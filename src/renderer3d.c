#include "renderer3d.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#include "display.h"

#define CAMERA_DISTANCE 4.0F
#define CAMERA_ORBIT_SPEED 0.075F
#define CAMERA_MAX_PITCH 1.25F
#define FOCAL_LENGTH 48.0F
#define SCREEN_CENTER_X 64
#define SCREEN_CENTER_Y 37
#define PI 3.14159265F

typedef struct {
    float x;
    float y;
    float z;
} vector3_t;

typedef struct {
    int16_t x;
    int16_t y;
    float depth;
} projected_vertex_t;

typedef struct {
    uint8_t first;
    uint8_t second;
} edge_t;

static const vector3_t cube_vertices[] = {
    {-1.0F, -1.0F, -1.0F}, { 1.0F, -1.0F, -1.0F},
    { 1.0F,  1.0F, -1.0F}, {-1.0F,  1.0F, -1.0F},
    {-1.0F, -1.0F,  1.0F}, { 1.0F, -1.0F,  1.0F},
    { 1.0F,  1.0F,  1.0F}, {-1.0F,  1.0F,  1.0F},
};

static const edge_t cube_edges[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static int rounded_integer(float value)
{
    return (int)(value >= 0.0F ? value + 0.5F : value - 0.5F);
}

static projected_vertex_t project_vertex(vector3_t vertex, const camera3d_t *camera)
{
    const float cosine_yaw = cosf(camera->yaw);
    const float sine_yaw = sinf(camera->yaw);
    const float yaw_x = cosine_yaw * vertex.x - sine_yaw * vertex.z;
    const float yaw_z = sine_yaw * vertex.x + cosine_yaw * vertex.z;

    const float cosine_pitch = cosf(camera->pitch);
    const float sine_pitch = sinf(camera->pitch);
    const float pitch_y = cosine_pitch * vertex.y - sine_pitch * yaw_z;
    const float pitch_z = sine_pitch * vertex.y + cosine_pitch * yaw_z;
    const float depth = pitch_z + CAMERA_DISTANCE;

    return (projected_vertex_t){
        .x = (int16_t)(SCREEN_CENTER_X + rounded_integer(yaw_x * FOCAL_LENGTH / depth)),
        .y = (int16_t)(SCREEN_CENTER_Y + rounded_integer(pitch_y * FOCAL_LENGTH / depth)),
        .depth = depth,
    };
}

static void draw_line(projected_vertex_t start, projected_vertex_t end, bool dotted)
{
    int x = start.x;
    int y = start.y;
    const int delta_x = end.x > start.x ? end.x - start.x : start.x - end.x;
    const int delta_y = end.y > start.y ? end.y - start.y : start.y - end.y;
    const int step_x = start.x < end.x ? 1 : -1;
    const int step_y = start.y < end.y ? 1 : -1;
    int error = delta_x - delta_y;
    int pixel_index = 0;

    while (true) {
        if (!dotted || (pixel_index & 1) == 0) {
            display_draw_pixel(x, y, true);
        }
        if (x == end.x && y == end.y) {
            break;
        }
        const int doubled_error = error * 2;
        if (doubled_error > -delta_y) {
            error -= delta_y;
            x += step_x;
        }
        if (doubled_error < delta_x) {
            error += delta_x;
            y += step_y;
        }
        ++pixel_index;
    }
}

void renderer3d_reset_camera(camera3d_t *camera)
{
    camera->yaw = 0.65F;
    camera->pitch = -0.40F;
}

void renderer3d_orbit_camera(camera3d_t *camera, float horizontal, float vertical)
{
    camera->yaw += horizontal * CAMERA_ORBIT_SPEED;
    camera->pitch += vertical * CAMERA_ORBIT_SPEED;
    if (camera->pitch > CAMERA_MAX_PITCH) {
        camera->pitch = CAMERA_MAX_PITCH;
    } else if (camera->pitch < -CAMERA_MAX_PITCH) {
        camera->pitch = -CAMERA_MAX_PITCH;
    }
    if (camera->yaw > PI) {
        camera->yaw -= 2.0F * PI;
    } else if (camera->yaw < -PI) {
        camera->yaw += 2.0F * PI;
    }
}

void renderer3d_draw_cube(const camera3d_t *camera)
{
    projected_vertex_t projected[8];
    for (size_t index = 0; index < sizeof(cube_vertices) / sizeof(cube_vertices[0]); ++index) {
        projected[index] = project_vertex(cube_vertices[index], camera);
    }

    // Draw farther edges first and dotted, then overlay nearer solid edges.
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t index = 0; index < sizeof(cube_edges) / sizeof(cube_edges[0]); ++index) {
            const edge_t edge = cube_edges[index];
            const bool far_edge =
                (projected[edge.first].depth + projected[edge.second].depth) * 0.5F >
                CAMERA_DISTANCE;
            if ((pass == 0) != far_edge) {
                continue;
            }
            draw_line(projected[edge.first], projected[edge.second], far_edge);
        }
    }

    for (size_t index = 0; index < sizeof(projected) / sizeof(projected[0]); ++index) {
        if (projected[index].depth <= CAMERA_DISTANCE) {
            display_fill_rectangle(projected[index].x - 1, projected[index].y - 1, 2, 2);
        }
    }
}
