#pragma once

#include "raylib.h"
#include "raymath.h"
#include <cmath>

class FlyCamera {
public:
    Camera3D camera;
    float yaw;
    float pitch;
    
    // Movement configurations
    float maxSpeed = 500.0f;
    float acceleration = 1000.0f;
    float damping = 8.0f;
    float mouseSensitivity = 0.15f;

    // Smooth movement state
    Vector3 velocity = { 0.0f, 0.0f, 0.0f };
    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    bool isCursorLocked = false;

    FlyCamera(Vector3 startPos = { 8.0f, 40.0f, 8.0f }) {
        camera.position = startPos;
        camera.target = Vector3Add(startPos, Vector3{ 0.0f, 0.0f, 1.0f });
        camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
        camera.fovy = 70.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        // Initialize yaw and pitch based on starting forward vector (0, 0, 1)
        yaw = 90.0f * DEG2RAD;
        pitch = 0.0f;
        targetYaw = yaw;
        targetPitch = pitch;
    }

    void update(float dt) {
        // Adjust camera velocity/speed with mouse scroll wheel
        float wheel = GetMouseWheelMove();
        if (std::abs(wheel) > 0.001f) {
            // Dynamic scroll step for effortless scrolling between 3 and 1000 m/s
            maxSpeed += wheel * (maxSpeed * 0.15f + 2.0f);
            if (maxSpeed < 3.0f) maxSpeed = 3.0f;
            if (maxSpeed > 1000.0f) maxSpeed = 1000.0f;
            
            // Adjust acceleration proportionally so handling remains snappy
            acceleration = maxSpeed * 2.0f;
        }

        // Toggle camera looking via Right Mouse Button
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            if (!isCursorLocked) {
                DisableCursor();
                isCursorLocked = true;
            }
        } else {
            if (isCursorLocked) {
                EnableCursor();
                isCursorLocked = false;
            }
        }

        // Process mouse look rotation only when cursor is disabled
        if (isCursorLocked) {
            Vector2 mouseDelta = GetMouseDelta();
            targetYaw -= mouseDelta.x * mouseSensitivity * DEG2RAD * 0.5f;
            targetPitch -= mouseDelta.y * mouseSensitivity * DEG2RAD * 0.5f;

            // Clamp pitch to avoid flips [-89, 89] degrees
            float pitchLimit = 89.0f * DEG2RAD;
            if (targetPitch > pitchLimit) targetPitch = pitchLimit;
            if (targetPitch < -pitchLimit) targetPitch = -pitchLimit;
        }

        // Smoothly interpolate camera rotation
        yaw = Lerp(yaw, targetYaw, 15.0f * dt);
        pitch = Lerp(pitch, targetPitch, 15.0f * dt);

        // Compute forward, right, and up vectors
        Vector3 forward = {
            cosf(pitch) * sinf(yaw),
            sinf(pitch),
            cosf(pitch) * cosf(yaw)
        };
        forward = Vector3Normalize(forward);

        Vector3 right = {
            sinf(yaw - PI/2.0f),
            0.0f,
            cosf(yaw - PI/2.0f)
        };
        right = Vector3Normalize(right);

        // Build target input force
        Vector3 moveInput = { 0.0f, 0.0f, 0.0f };
        if (IsKeyDown(KEY_W)) moveInput = Vector3Add(moveInput, forward);
        if (IsKeyDown(KEY_S)) moveInput = Vector3Subtract(moveInput, forward);
        if (IsKeyDown(KEY_D)) moveInput = Vector3Add(moveInput, right);
        if (IsKeyDown(KEY_A)) moveInput = Vector3Subtract(moveInput, right);
        if (IsKeyDown(KEY_SPACE)) moveInput.y += 1.0f;        // Move directly up
        if (IsKeyDown(KEY_LEFT_SHIFT)) moveInput.y -= 1.0f;   // Move directly down

        // Normalize moveInput to ensure diagonal flight isn't faster
        if (Vector3Length(moveInput) > 0.001f) {
            moveInput = Vector3Normalize(moveInput);
        }

        // Accelerate velocity based on inputs
        velocity = Vector3Add(velocity, Vector3Scale(moveInput, acceleration * dt));

        // Apply damping drag to stop smoothly
        velocity = Vector3Subtract(velocity, Vector3Scale(velocity, damping * dt));

        // Clamp speed to maximum flight speed
        float speed = Vector3Length(velocity);
        if (speed > maxSpeed) {
            velocity = Vector3Scale(Vector3Normalize(velocity), maxSpeed);
        }

        // Apply velocity to position
        camera.position = Vector3Add(camera.position, Vector3Scale(velocity, dt));

        // Update target looking point based on updated position and rotation direction
        camera.target = Vector3Add(camera.position, forward);
    }
};
