#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include <glob.h>

#include "mathc.h"
#include "rammel.h"
#include "input.h"
#include "graphics.h"
#include "model.h"
#include "image.h"
#include "voxel.h"

static float billboard_angle = 0.0f;
static float billboard_scale = 1.0f;
static float billboard_height = 0.0f;
static float auto_rotate_speed = 0.5f;
static bool auto_rotate = true;

static int image_count = 0;
static char** image_list = NULL;
static int image_current = 0;

static image_t* current_image = NULL;
static model_t* billboard_model = NULL;

static model_t* create_billboard_model(image_t* image) {
    if (!image) {
        return NULL;
    }

    model_t* model = (model_t*)malloc(sizeof(model_t));
    memset(model, 0, sizeof(*model));

    float aspect = (float)image->width / (float)image->height;

    float hw, hh;
    if (aspect >= 1.0f) {
        hw = (VOXELS_X - 1) * 0.45f;
        hh = hw / aspect;
    } else {
        hh = (VOXELS_Z - 1) * 0.45f;
        hw = hh * aspect;
    }

    model->vertex_count = 4;
    model->vertices = malloc(model->vertex_count * sizeof(vertex_t));

    model->vertices[0].position = (vec3_t){{-hw, 0, -hh}};
    model->vertices[0].texcoord = (vec2_t){{0, 0}};

    model->vertices[1].position = (vec3_t){{ hw, 0, -hh}};
    model->vertices[1].texcoord = (vec2_t){{1, 0}};

    model->vertices[2].position = (vec3_t){{ hw, 0,  hh}};
    model->vertices[2].texcoord = (vec2_t){{1, 1}};

    model->vertices[3].position = (vec3_t){{-hw, 0,  hh}};
    model->vertices[3].texcoord = (vec2_t){{0, 1}};

    model->surface_count = 2;
    model->surfaces = malloc(model->surface_count * sizeof(surface_t));

    // front face
    model->surfaces[0].colour = HEXPIX(FFFFFF);
    model->surfaces[0].index_count = 6;
    model->surfaces[0].indices = malloc(6 * sizeof(index_t));
    index_t front_indices[] = {0, 1, 2, 0, 2, 3};
    memcpy(model->surfaces[0].indices, front_indices, sizeof(front_indices));
    model->surfaces[0].image = image;

    // back face (reversed winding so image shows on both sides)
    model->surfaces[1].colour = HEXPIX(FFFFFF);
    model->surfaces[1].index_count = 6;
    model->surfaces[1].indices = malloc(6 * sizeof(index_t));
    index_t back_indices[] = {2, 1, 0, 3, 2, 0};
    memcpy(model->surfaces[1].indices, back_indices, sizeof(back_indices));
    model->surfaces[1].image = image;

    return model;
}

static void free_billboard_model(model_t* model) {
    if (model) {
        free(model->vertices);
        if (model->surfaces) {
            // don't free the image via surface; we manage it separately
            for (int i = 0; i < model->surface_count; ++i) {
                free(model->surfaces[i].indices);
                model->surfaces[i].image = NULL;
            }
            free(model->surfaces);
        }
        free(model->edges);
        free(model);
    }
}

static bool load_billboard_image(const char* filename) {
    if (current_image) {
        image_free(current_image);
        current_image = NULL;
    }
    if (billboard_model) {
        free_billboard_model(billboard_model);
        billboard_model = NULL;
    }

    printf("loading billboard: %s\n", filename);

    current_image = image_load(filename);
    if (!current_image) {
        printf("failed to load image: %s\n", filename);
        return false;
    }

    printf("image: %dx%d\n", current_image->width, current_image->height);

    billboard_model = create_billboard_model(current_image);
    billboard_scale = 1.0f;
    billboard_height = 0.0f;

    return billboard_model != NULL;
}

static void parse_arguments(int argc, char** argv) {
    glob_t glob_result;
    memset(&glob_result, 0, sizeof(glob_result));
    int file_count = 0;
    char** file_list = NULL;

    for (int i = 1; i < argc; ++i) {
        int ret = glob(argv[i], GLOB_TILDE, NULL, &glob_result);
        if (ret != 0) {
            globfree(&glob_result);
            continue;
        }

        file_list = realloc(file_list, (file_count + glob_result.gl_pathc) * sizeof(char*));
        if (!file_list) {
            globfree(&glob_result);
            return;
        }

        for (size_t j = 0; j < glob_result.gl_pathc; ++j) {
            file_list[file_count + j] = strdup(glob_result.gl_pathv[j]);
        }

        file_count += glob_result.gl_pathc;
        globfree(&glob_result);
    }

    if (file_list) {
        image_count = file_count;
        image_list = file_list;
    }
}

int main(int argc, char** argv) {
    parse_arguments(argc, argv);

    if (image_count == 0) {
        printf("usage: billboard <image.png> [image2.png ...]\n");
        printf("  displays images as rotating 3D billboards\n");
        return 1;
    }

    if (!voxel_buffer_map()) {
        printf("waiting for voxel buffer\n");
        do {
            sleep(1);
        } while (!voxel_buffer_map());
    }

    load_billboard_image(image_list[0]);

    mfloat_t centre[VEC3_SIZE] = {
        (VOXELS_X - 1) * 0.5f,
        (VOXELS_Y - 1) * 0.5f,
        (VOXELS_Z - 1) * 0.5f
    };

    float matrix[MAT4_SIZE];

    input_set_nonblocking();

    int image_target = image_current;

    for (int ch = 0; ch != 27; ch = getchar()) {

        switch (ch) {
            case ' ':
                auto_rotate = !auto_rotate;
                break;
            case '+':
            case '=':
                auto_rotate_speed += 0.1f;
                break;
            case '-':
                auto_rotate_speed -= 0.1f;
                break;
            case '[':
                image_target -= 1;
                break;
            case ']':
                image_target += 1;
                break;
        }

        input_update();

        if (input_get_button(0, BUTTON_A, BUTTON_PRESSED)) {
            auto_rotate = !auto_rotate;
        }

        if (input_get_button(0, BUTTON_LB, BUTTON_PRESSED)) {
            image_target -= 1;
        }

        if (input_get_button(0, BUTTON_RB, BUTTON_PRESSED)) {
            image_target += 1;
        }

        // manual rotation
        float manual_rotate = input_get_axis(0, AXIS_LS_X) * -0.1f;
        if (!auto_rotate && fabsf(manual_rotate) > 0.001f) {
            billboard_angle += manual_rotate;
        }

        // scale control
        float dscale = (input_get_axis(0, AXIS_LT) - input_get_axis(0, AXIS_RT)) * 0.05f;
        billboard_scale *= 1.0f + dscale;
        billboard_scale = clamp(billboard_scale, 0.1f, 5.0f);

        // height offset
        float dheight = input_get_axis(0, AXIS_RS_Y) * -1.0f;
        billboard_height += dheight;
        billboard_height = clamp(billboard_height, -(float)(VOXELS_Z - 1) * 0.4f, (float)(VOXELS_Z - 1) * 0.4f);

        // switch images
        if (image_count > 0) {
            image_target = modulo(image_target, image_count);
            if (image_current != image_target) {
                image_current = image_target;
                load_billboard_image(image_list[image_current]);
            }
        }

        // auto rotation
        if (auto_rotate) {
            billboard_angle += auto_rotate_speed * 0.02f;
            if (billboard_angle > M_PI * 2) {
                billboard_angle -= M_PI * 2;
            }
        }

        // draw
        if (billboard_model) {
            pixel_t* volume = voxel_buffer_get(VOXEL_BUFFER_BACK);
            voxel_buffer_clear(volume);

            mat4_identity(matrix);
            mat4_apply_translation(matrix, centre);

            float offset[VEC3_SIZE] = {0, 0, billboard_height};
            mat4_apply_translation(matrix, offset);

            mat4_apply_scale_f(matrix, billboard_scale);
            mat4_apply_rotation_z(matrix, billboard_angle);

            model_draw(volume, billboard_model, matrix);

            voxel_buffer_swap();
        }

        usleep(20000);
    }

    free_billboard_model(billboard_model);
    billboard_model = NULL;
    if (current_image) {
        image_free(current_image);
        current_image = NULL;
    }

    for (int i = 0; i < image_count; ++i) {
        free(image_list[i]);
    }
    free(image_list);

    voxel_buffer_unmap();

    return 0;
}
