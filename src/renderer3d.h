#pragma once

typedef struct {
    float yaw;
    float pitch;
} camera3d_t;

void renderer3d_reset_camera(camera3d_t *camera);
void renderer3d_orbit_camera(camera3d_t *camera, float horizontal, float vertical);
void renderer3d_draw_cube(const camera3d_t *camera);
