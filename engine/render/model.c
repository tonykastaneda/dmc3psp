#include "model.h"
#include "model_runtime.h"

typedef unsigned int u32;
typedef unsigned char u8;

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
#define FRAMEBUFFER_WIDTH 512
#define RENDER_WIDTH 240
#define RENDER_HEIGHT 136
#define ROTATE_COS 0.8660254f
#define ROTATE_SIN 0.5f

static float depth_buffer[RENDER_WIDTH * RENDER_HEIGHT];
static u32 color_buffer[RENDER_WIDTH * RENDER_HEIGHT];
static float screen_x[DMC3_MODEL_VERTEX_COUNT];
static float screen_y[DMC3_MODEL_VERTEX_COUNT];
static float screen_z[DMC3_MODEL_VERTEX_COUNT];
static float vertex_light[DMC3_MODEL_VERTEX_COUNT];

static float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void transform_position(Dmc3Vec3 position, float *x, float *y, float *z) {
    float dx = position.x - DMC3_MODEL_CENTER_X;
    float dy = position.y - DMC3_MODEL_CENTER_Y;
    float dz = position.z - DMC3_MODEL_CENTER_Z;
    float rx = dx * ROTATE_COS - dz * ROTATE_SIN;
    *z = dx * ROTATE_SIN + dz * ROTATE_COS;
    *x = 120.0f + rx * (DMC3_MODEL_SCALE * 0.5f);
    *y = 68.0f - dy * (DMC3_MODEL_SCALE * 0.5f);
}

static void upscale_to_framebuffer(u32 *framebuffer) {
    int x;
    int y;
    for (y = 0; y < RENDER_HEIGHT; ++y) {
        for (x = 0; x < RENDER_WIDTH; ++x) {
            u32 color = color_buffer[y * RENDER_WIDTH + x];
            u32 *destination;
            if (color == 0) {
                continue;
            }
            destination = &framebuffer[(y * 2) * FRAMEBUFFER_WIDTH + x * 2];
            destination[0] = color;
            destination[1] = color;
            destination[FRAMEBUFFER_WIDTH] = color;
            destination[FRAMEBUFFER_WIDTH + 1] = color;
        }
    }
}

int dmc3_model_render(u32 *framebuffer) {
    int accepted = 0;
    int i;
    int object_index;
    for (i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; ++i) {
        depth_buffer[i] = 1.0e30f;
        color_buffer[i] = 0;
    }
    for (i = 0; i < DMC3_MODEL_VERTEX_COUNT; ++i) {
        Dmc3Vec3 normal = dmc3_model_normals[i];
        float nz = normal.x * ROTATE_SIN + normal.z * ROTATE_COS;
        transform_position(dmc3_model_positions[i], &screen_x[i], &screen_y[i], &screen_z[i]);
        if (nz < 0.0f) {
            nz = -nz;
        }
        vertex_light[i] = 0.65f + nz * 0.35f;
    }

    for (object_index = 0; object_index < DMC3_MODEL_OBJECT_COUNT; ++object_index) {
        Dmc3ModelObject object = dmc3_model_objects[object_index];
        Dmc3Texture texture = dmc3_model_textures[object.texture];
        unsigned int triangle;
        for (triangle = object.index_start; triangle < object.index_start + object.index_count; triangle += 3) {
            unsigned int i0 = dmc3_model_indices[triangle];
            unsigned int i1 = dmc3_model_indices[triangle + 1];
            unsigned int i2 = dmc3_model_indices[triangle + 2];
            float x0 = screen_x[i0];
            float y0 = screen_y[i0];
            float z0 = screen_z[i0];
            float x1 = screen_x[i1];
            float y1 = screen_y[i1];
            float z1 = screen_z[i1];
            float x2 = screen_x[i2];
            float y2 = screen_y[i2];
            float z2 = screen_z[i2];
            float area = edge(x0, y0, x1, y1, x2, y2);
            float inverse_area;
            int min_x;
            int max_x;
            int min_y;
            int max_y;
            int x;
            int y;
            if (area > -0.000001f && area < 0.000001f) {
                continue;
            }
            inverse_area = 1.0f / area;
            min_x = (int)(x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2));
            max_x = (int)(x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2)) + 1;
            min_y = (int)(y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2));
            max_y = (int)(y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2)) + 1;
            if (min_x < 0) min_x = 0;
            if (min_y < 0) min_y = 0;
            if (max_x > RENDER_WIDTH) max_x = RENDER_WIDTH;
            if (max_y > RENDER_HEIGHT) max_y = RENDER_HEIGHT;
            for (y = min_y; y < max_y; ++y) {
                for (x = min_x; x < max_x; ++x) {
                    float px = (float)x + 0.5f;
                    float py = (float)y + 0.5f;
                    float w0 = edge(x1, y1, x2, y2, px, py) * inverse_area;
                    float w1 = edge(x2, y2, x0, y0, px, py) * inverse_area;
                    float w2 = 1.0f - w0 - w1;
                    float depth;
                    float u;
                    float v;
                    float light;
                    int tx;
                    int ty;
                    const u8 *texel;
                    u32 r;
                    u32 g;
                    u32 b;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                        continue;
                    }
                    depth = w0 * z0 + w1 * z1 + w2 * z2;
                    if (depth >= depth_buffer[y * RENDER_WIDTH + x]) {
                        continue;
                    }
                    u = w0 * dmc3_model_uvs[i0].u + w1 * dmc3_model_uvs[i1].u + w2 * dmc3_model_uvs[i2].u;
                    v = w0 * dmc3_model_uvs[i0].v + w1 * dmc3_model_uvs[i1].v + w2 * dmc3_model_uvs[i2].v;
                    if (u < 0.0f) u = 0.0f;
                    if (u > 1.0f) u = 1.0f;
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    tx = (int)(u * (float)(texture.width - 1));
                    ty = (int)(v * (float)(texture.height - 1));
                    texel = &texture.rgba[(ty * texture.width + tx) * 4];
                    if (texel[3] < 128) {
                        continue;
                    }
                    light = w0 * vertex_light[i0] + w1 * vertex_light[i1] + w2 * vertex_light[i2];
                    r = (u32)(texel[0] * light);
                    g = (u32)(texel[1] * light);
                    b = (u32)(texel[2] * light);
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                    color_buffer[y * RENDER_WIDTH + x] = 0xff000000u | (b << 16) | (g << 8) | r;
                    depth_buffer[y * RENDER_WIDTH + x] = depth;
                    ++accepted;
                }
            }
        }
    }
    upscale_to_framebuffer(framebuffer);
    return accepted;
}
