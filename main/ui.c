#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "buttons.h"
#include "display.h"
#include "esp_log.h"
#include "http.h"
#include "species.h"
#include "thread.h"
#include "ui.h"
#include "bird_drawing.h"
#include "send_audio.h"

static const char *TAG = "ui";

void draw_sprite_for_species(const char* str) {
    char scientific[128];
    strncpy(scientific, str, sizeof(scientific));
    char *_pos = strchr(scientific, '_');
    if (_pos) {
        *_pos = '\0';
    }
    dibujar_pajaro_dada_respuesta(scientific);
}

#define ITEMS_PER_PAGE 4
#define MAX_LATEST_SPECIES 3
#define MAX_AUDIOS 64
#define MAX_SPECIES_PER_AUDIO 32
#define MAX_ALL_SPECIES 64
#define BUFFER_SIZE 4096
#define NAME_BUFFER_SIZE 128

typedef enum {
    UI_STATE_SCREENSAVER,
    UI_STATE_MAIN_MENU,
    UI_STATE_RECENT_AUDIOS,
    UI_STATE_AUDIO_SPECIES,
    UI_STATE_ALL_SPECIES
} ui_state_t;

typedef struct {
    int id;
    char datetime[32];
} audio_record_t;

typedef struct {
    char name[NAME_BUFFER_SIZE];
    int count;
} species_stat_t;

static ui_state_t current_state = UI_STATE_SCREENSAVER;

static char latest_species[MAX_LATEST_SPECIES][NAME_BUFFER_SIZE];
static int latest_species_count = 0;
static bool latest_species_loaded = false;

static int carousel_index = 0;
static int carousel_last_switch_time = 0;

static const char *main_menu_items[] = {
    "1. Recent Audios",
    "2. All Birds Count",
    "3. Screensaver"
};
static int main_menu_count = 3;
static int main_menu_selected_idx = 0;

static audio_record_t audio_list[MAX_AUDIOS];
static int audio_list_count = 0;
static int audio_selected_idx = 0;

static int selected_audio_id = -1;
static char audio_species_list[MAX_SPECIES_PER_AUDIO][NAME_BUFFER_SIZE];
static int audio_species_count = 0;
static int audio_species_selected_idx = 0;

static species_stat_t all_species_list[MAX_ALL_SPECIES];
static int all_species_count = 0;
static int all_species_selected_idx = 0;

static char http_buffer[BUFFER_SIZE];

static void update_latest_species(void) {
    int status_code = make_http_get_request("/latest_species", http_buffer);
    if (status_code != 200) {
        return;
    }

    latest_species_count = 0;
    char *cursor = http_buffer;
    while (*cursor && latest_species_count < MAX_LATEST_SPECIES) {
        char *newline_pos = strchr(cursor, '\n');
        if (newline_pos) {
            *newline_pos = '\0';
        }

        if (strlen(cursor) > 0) {
            strncpy(latest_species[latest_species_count], cursor, NAME_BUFFER_SIZE - 1);
            latest_species[latest_species_count][NAME_BUFFER_SIZE - 1] = '\0';
            latest_species_count++;
        }

        if (!newline_pos) {
            break;
        }
        cursor = newline_pos + 1;
    }
    latest_species_loaded = true;
    carousel_index = 0;
    carousel_last_switch_time = current_time();
}

static void fetch_recent_audios(void) {
    audio_list_count = 0;
    audio_selected_idx = 0;

    int status_code = make_http_get_request("/recent_audios?limit=64&offset=0", http_buffer);
    if (status_code != 200) {
        return;
    }

    char *cursor = http_buffer;
    while (*cursor && audio_list_count < MAX_AUDIOS) {
        char *newline_pos = strchr(cursor, '\n');
        if (newline_pos) {
            *newline_pos = '\0';
        }

        if (strlen(cursor) > 0) {
            int audio_id = 0;
            char audio_dt[32] = {0};
            if (sscanf(cursor, "%d %31s", &audio_id, audio_dt) >= 1) {
                audio_list[audio_list_count].id = audio_id;
                strncpy(audio_list[audio_list_count].datetime, audio_dt, sizeof(audio_list[audio_list_count].datetime) - 1);
                audio_list_count++;
            }
        }

        if (!newline_pos) {
            break;
        }
        cursor = newline_pos + 1;
    }
}

static void fetch_audio_species(int audio_id) {
    audio_species_count = 0;
    audio_species_selected_idx = 0;
    selected_audio_id = audio_id;

    char path[64];
    snprintf(path, sizeof(path), "/audios/%d/species", audio_id);

    int status_code = make_http_get_request(path, http_buffer);
    if (status_code != 200) {
        return;
    }

    char *cursor = http_buffer;
    while (*cursor && audio_species_count < MAX_SPECIES_PER_AUDIO) {
        char *newline_pos = strchr(cursor, '\n');
        if (newline_pos) {
            *newline_pos = '\0';
        }

        if (strlen(cursor) > 0) {
            strncpy(audio_species_list[audio_species_count], cursor, NAME_BUFFER_SIZE - 1);
            audio_species_list[audio_species_count][NAME_BUFFER_SIZE - 1] = '\0';
            audio_species_count++;
        }

        if (!newline_pos) {
            break;
        }
        cursor = newline_pos + 1;
    }
}

static void fetch_all_species(void) {
    all_species_count = 0;
    all_species_selected_idx = 0;

    int status_code = make_http_get_request("/all_species", http_buffer);
    if (status_code != 200) {
        return;
    }

    char *cursor = http_buffer;
    while (*cursor && all_species_count < MAX_ALL_SPECIES) {
        char *newline_pos = strchr(cursor, '\n');
        if (newline_pos) {
            *newline_pos = '\0';
        }

        if (strlen(cursor) > 0) {
            char *colon_pos = strrchr(cursor, ':');
            if (colon_pos) {
                *colon_pos = '\0';
                char *name_part = cursor;
                char *count_part = colon_pos + 1;
                while (*count_part == ' ') {
                    count_part++;
                }

                strncpy(all_species_list[all_species_count].name, name_part, NAME_BUFFER_SIZE - 1);
                all_species_list[all_species_count].name[NAME_BUFFER_SIZE - 1] = '\0';
                all_species_list[all_species_count].count = atoi(count_part);
                all_species_count++;
            } else {
                strncpy(all_species_list[all_species_count].name, cursor, NAME_BUFFER_SIZE - 1);
                all_species_list[all_species_count].name[NAME_BUFFER_SIZE - 1] = '\0';
                all_species_list[all_species_count].count = 0;
                all_species_count++;
            }
        }

        if (!newline_pos) {
            break;
        }
        cursor = newline_pos + 1;
    }
}

static void render_screensaver(void) {
    if (!latest_species_loaded) {
        update_latest_species();
    }

    display_fill_rect(0, 0, WIDTH, HEIGHT, false);

    if (latest_species_count == 0) {
        display_text(10, 24, "No species sighted", false);
        display_text(10, 48, "[KEY1: Update]", false);
        return;
    }

    int now = current_time();
    if (time_elapsed_between_ms(carousel_last_switch_time, now) >= CAROUSEL_DURATION) {
        carousel_index = (carousel_index + 1) % latest_species_count;
        carousel_last_switch_time = now;
    }

    const char *species_name = latest_species[carousel_index];
    draw_sprite_for_species(species_name);

    const char *label = common_name_from_species_name(species_name);
    if (!label || strlen(label) == 0) {
        label = species_name;
    }

    char display_buf[32];
    snprintf(display_buf, sizeof(display_buf), "%.20s", label);
    display_text(0, 52, display_buf, false);
}

static void render_main_menu(void) {
    display_fill_rect(0, 0, WIDTH, HEIGHT, false);
    display_text(0, 0, "-- MAIN MENU --", false);

    for (int idx = 0; idx < main_menu_count; idx++) {
        int y_pos = 14 + idx * 12;
        bool is_selected = (idx == main_menu_selected_idx);
        display_text(4, y_pos, main_menu_items[idx], is_selected);
    }

    display_text(0, 52, "[UP/DN] [R:Select]", false);
}

static void render_recent_audios(void) {
    display_fill_rect(0, 0, WIDTH, HEIGHT, false);

    if (audio_list_count == 0) {
        display_text(0, 0, "Recent Audios", false);
        display_text(4, 24, "No audios found", false);
        display_text(0, 52, "[L: Back]", false);
        return;
    }

    int current_page = audio_selected_idx / ITEMS_PER_PAGE;
    int total_pages = (audio_list_count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int page_start = current_page * ITEMS_PER_PAGE;

    char header_buf[64];
    snprintf(header_buf, sizeof(header_buf), "Audios (%d/%d)", current_page + 1, total_pages);
    display_text(0, 0, header_buf, false);

    for (int offset = 0; offset < ITEMS_PER_PAGE; offset++) {
        int item_idx = page_start + offset;
        if (item_idx >= audio_list_count) {
            break;
        }

        int y_pos = 12 + offset * 10;
        bool is_selected = (item_idx == audio_selected_idx);

        char line_buf[64];
        snprintf(line_buf, sizeof(line_buf), "#%d %.15s", audio_list[item_idx].id, audio_list[item_idx].datetime);
        display_text(4, y_pos, line_buf, is_selected);
    }

    display_text(0, 52, "[L:Back] [R:Species]", false);
}

static void render_audio_species(void) {
    display_fill_rect(0, 0, WIDTH, HEIGHT, false);

    if (audio_species_count == 0) {
        char header_buf[64];
        snprintf(header_buf, sizeof(header_buf), "Audio #%d Species", selected_audio_id);
        display_text(0, 0, header_buf, false);
        display_text(4, 24, "No species found", false);
        display_text(0, 52, "[L: Back]", false);
        return;
    }

    int current_page = audio_species_selected_idx / ITEMS_PER_PAGE;
    int total_pages = (audio_species_count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int page_start = current_page * ITEMS_PER_PAGE;

    char header_buf[64];
    snprintf(header_buf, sizeof(header_buf), "Audio #%d (%d/%d)", selected_audio_id, current_page + 1, total_pages);
    display_text(0, 0, header_buf, false);

    for (int offset = 0; offset < ITEMS_PER_PAGE; offset++) {
        int item_idx = page_start + offset;
        if (item_idx >= audio_species_count) {
            break;
        }

        int y_pos = 12 + offset * 10;
        bool is_selected = (item_idx == audio_species_selected_idx);

        const char *species_name = audio_species_list[item_idx];
        const char *label = common_name_from_species_name(species_name);
        if (!label || strlen(label) == 0) {
            label = species_name;
        }

        char line_buf[64];
        snprintf(line_buf, sizeof(line_buf), "%.20s", label);
        display_text(4, y_pos, line_buf, is_selected);
    }

    display_text(0, 52, "[L: Back]", false);
}

static void render_all_species(void) {
    display_fill_rect(0, 0, WIDTH, HEIGHT, false);

    if (all_species_count == 0) {
        display_text(0, 0, "All Species Count", false);
        display_text(4, 24, "No records found", false);
        display_text(0, 52, "[L: Back]", false);
        return;
    }

    int current_page = all_species_selected_idx / ITEMS_PER_PAGE;
    int total_pages = (all_species_count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int page_start = current_page * ITEMS_PER_PAGE;

    char header_buf[64];
    snprintf(header_buf, sizeof(header_buf), "All Birds (%d/%d)", current_page + 1, total_pages);
    display_text(0, 0, header_buf, false);

    for (int offset = 0; offset < ITEMS_PER_PAGE; offset++) {
        int item_idx = page_start + offset;
        if (item_idx >= all_species_count) {
            break;
        }

        int y_pos = 12 + offset * 10;
        bool is_selected = (item_idx == all_species_selected_idx);

        const char *species_name = all_species_list[item_idx].name;
        const char *label = common_name_from_species_name(species_name);
        if (!label || strlen(label) == 0) {
            label = species_name;
        }

        char line_buf[64];
        snprintf(line_buf, sizeof(line_buf), "%d: %.17s", all_species_list[item_idx].count, label);
        display_text(4, y_pos, line_buf, is_selected);
    }

    display_text(0, 52, "[L: Back]", false);
}

void draw_ui(void) {
    button_event_t event;
    while (unqueue_button_event(&event)) {
        if (!event.pressed) {
            continue;
        }
        ESP_LOGI(TAG, "button %s %s", buttons_name(event.id), event.pressed ? "pressed" : "released");

        if (current_state == UI_STATE_SCREENSAVER) {
            if (event.id == BTN_KEY1) {
                update_latest_species();
            } else {
                current_state = UI_STATE_MAIN_MENU;
                main_menu_selected_idx = 0;
            }
            continue;
        }

        switch (current_state) {
            case UI_STATE_MAIN_MENU:
                if (event.id == BTN_UP) {
                    main_menu_selected_idx = (main_menu_selected_idx - 1 + main_menu_count) % main_menu_count;
                } else if (event.id == BTN_DOWN) {
                    main_menu_selected_idx = (main_menu_selected_idx + 1) % main_menu_count;
                } else if (event.id == BTN_RIGHT || event.id == BTN_KEY1) {
                    if (main_menu_selected_idx == 0) {
                        current_state = UI_STATE_RECENT_AUDIOS;
                        fetch_recent_audios();
                    } else if (main_menu_selected_idx == 1) {
                        current_state = UI_STATE_ALL_SPECIES;
                        fetch_all_species();
                    } else if (main_menu_selected_idx == 2) {
                        current_state = UI_STATE_SCREENSAVER;
                        carousel_last_switch_time = current_time();
                    }
                } else if (event.id == BTN_LEFT) {
                    current_state = UI_STATE_SCREENSAVER;
                    carousel_last_switch_time = current_time();
                }
                break;

            case UI_STATE_RECENT_AUDIOS:
                if (event.id == BTN_UP) {
                    if (audio_selected_idx > 0) {
                        audio_selected_idx--;
                    }
                } else if (event.id == BTN_DOWN) {
                    if (audio_selected_idx + 1 < audio_list_count) {
                        audio_selected_idx++;
                    }
                } else if (event.id == BTN_KEY3) {
                    if (audio_selected_idx >= ITEMS_PER_PAGE) {
                        audio_selected_idx -= ITEMS_PER_PAGE;
                    } else {
                        audio_selected_idx = 0;
                    }
                } else if (event.id == BTN_KEY4) {
                    if (audio_selected_idx + ITEMS_PER_PAGE < audio_list_count) {
                        audio_selected_idx += ITEMS_PER_PAGE;
                    } else if (audio_list_count > 0) {
                        audio_selected_idx = audio_list_count - 1;
                    }
                } else if (event.id == BTN_RIGHT || event.id == BTN_KEY1) {
                    if (audio_list_count > 0) {
                        current_state = UI_STATE_AUDIO_SPECIES;
                        fetch_audio_species(audio_list[audio_selected_idx].id);
                    }
                } else if (event.id == BTN_LEFT || event.id == BTN_KEY2) {
                    current_state = UI_STATE_MAIN_MENU;
                }
                break;

            case UI_STATE_AUDIO_SPECIES:
                if (event.id == BTN_UP) {
                    if (audio_species_selected_idx > 0) {
                        audio_species_selected_idx--;
                    }
                } else if (event.id == BTN_DOWN) {
                    if (audio_species_selected_idx + 1 < audio_species_count) {
                        audio_species_selected_idx++;
                    }
                } else if (event.id == BTN_KEY3) {
                    if (audio_species_selected_idx >= ITEMS_PER_PAGE) {
                        audio_species_selected_idx -= ITEMS_PER_PAGE;
                    } else {
                        audio_species_selected_idx = 0;
                    }
                } else if (event.id == BTN_KEY4) {
                    if (audio_species_selected_idx + ITEMS_PER_PAGE < audio_species_count) {
                        audio_species_selected_idx += ITEMS_PER_PAGE;
                    } else if (audio_species_count > 0) {
                        audio_species_selected_idx = audio_species_count - 1;
                    }
                } else if (event.id == BTN_LEFT || event.id == BTN_KEY2) {
                    current_state = UI_STATE_RECENT_AUDIOS;
                }
                break;

            case UI_STATE_ALL_SPECIES:
                if (event.id == BTN_UP) {
                    if (all_species_selected_idx > 0) {
                        all_species_selected_idx--;
                    }
                } else if (event.id == BTN_DOWN) {
                    if (all_species_selected_idx + 1 < all_species_count) {
                        all_species_selected_idx++;
                    }
                } else if (event.id == BTN_KEY3) {
                    if (all_species_selected_idx >= ITEMS_PER_PAGE) {
                        all_species_selected_idx -= ITEMS_PER_PAGE;
                    } else {
                        all_species_selected_idx = 0;
                    }
                } else if (event.id == BTN_KEY4) {
                    if (all_species_selected_idx + ITEMS_PER_PAGE < all_species_count) {
                        all_species_selected_idx += ITEMS_PER_PAGE;
                    } else if (all_species_count > 0) {
                        all_species_selected_idx = all_species_count - 1;
                    }
                } else if (event.id == BTN_LEFT || event.id == BTN_KEY2) {
                    current_state = UI_STATE_MAIN_MENU;
                }
                break;

            default:
                break;
        }
    }

    if (inactivity_time_ms() > INACTIVITY_CUTOFF && current_state != UI_STATE_SCREENSAVER) {
        current_state = UI_STATE_SCREENSAVER;
        carousel_last_switch_time = current_time();
    }

    switch (current_state) {
        case UI_STATE_SCREENSAVER:
            render_screensaver();
            break;
        case UI_STATE_MAIN_MENU:
            render_main_menu();
            break;
        case UI_STATE_RECENT_AUDIOS:
            render_recent_audios();
            break;
        case UI_STATE_AUDIO_SPECIES:
            render_audio_species();
            break;
        case UI_STATE_ALL_SPECIES:
            render_all_species();
            break;
    }

    if (is_recording) {
        display_text(0, 0, "M", false);
    } else {
        display_text(0, 0, "0", false);
    }
}
