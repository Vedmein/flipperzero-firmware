//
// Created by moh on 30.11.2021.
//

#include <stdlib.h>
#include <string.h>

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <furi-hal-resources.h>

#define Y_FIELD_SIZE 6
#define Y_LAST (Y_FIELD_SIZE - 1)
#define X_FIELD_SIZE 12
#define X_LAST (X_FIELD_SIZE - 1)

#define DRAW_X_OFFSET 4

#define TAG "HeDe"

#define BOX_HEIGHT 10
#define BOX_WIDTH 10
#define TIMER_UPDATE_FREQ 8
#define BOX_GENERATION_RATE 15

static const Icon *boxes[5] = {
			&I_Box1_10x10,
            &I_Box2_10x10,
            &I_Box3_10x10,
            &I_Box4_10x10,
            &I_Box5_10x10
};

typedef enum {
    AnimationGameOver = 0,
    AnimationPause,
    AnimationLeft,
    AnimationRight,
} Animations;

static IconAnimation *animations[4];

typedef u_int8_t byte;

typedef enum {
    GameStatusOver,
    GameStatusPause,
    GameStatusInProgress,
    GameStatusExit
} GameStatuses;

typedef struct {
    uint8_t x;
    uint8_t y;
} Position;

typedef struct {
    Position    p;
    int8_t      x_direction;
    int8_t      j_tick;
    int8_t      h_tick;
    bool        right_frame;
} Person;

typedef struct {
    uint8_t offset: 4;
    uint8_t box_id: 3;
    uint8_t exists: 1;
} Box;

typedef struct {
    Box**        field;
    Person*      person;
	Animations   animation;
    GameStatuses game_status;
} GameState;

typedef Box** Field;

typedef enum {
    EventGameTick = 0,
    EventKeyPress
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} GameEvent;

/**
 * #Construct / Destroy
 */

static void game_reset_field_and_player(GameState* game) {
    ///Reset field
    bzero(game->field[0], X_FIELD_SIZE * Y_FIELD_SIZE * sizeof(Box));

    ///Reset person
    bzero(game->person, sizeof(Person));
    game->person->p.x = X_FIELD_SIZE / 2;
    game->person->p.y = Y_LAST;
}

static GameState* allocGameState() {
    GameState* game = furi_alloc(sizeof(GameState));

    game->person = furi_alloc(sizeof(Person));

    game->field = furi_alloc(Y_FIELD_SIZE * sizeof(Box*));
    game->field[0] = furi_alloc(X_FIELD_SIZE * Y_FIELD_SIZE * sizeof(Box));
    for(int y = 1; y < Y_FIELD_SIZE; ++y) {
        game->field[y] = game->field[0] + (y * X_FIELD_SIZE);
    }
    game_reset_field_and_player(game);

    game->game_status = GameStatusInProgress;
    return game;
}

static void game_destroy(GameState* game) {
    furi_assert(game);
    free(game->field[0]);
    free(game->field);
    free(game);
}

static void animations_alloc_and_start() {

    animations[AnimationPause] = icon_animation_alloc(&A_HD_start_128x64);
    animations[AnimationGameOver] = icon_animation_alloc(&A_HD_game_over_128x64);
    animations[AnimationLeft] = icon_animation_alloc(&A_HD_person_left_10x20);
    animations[AnimationRight] = icon_animation_alloc(&A_HD_person_right_10x20);

	for (int i = 0; i < 4; ++i) {
        furi_assert(animations[i]);
		icon_animation_start(animations[i]);
	}
}

static void animations_stop_and_destroy() {
	for (int i = 0; i < 4; ++i) {
		icon_animation_free(animations[i]);
	}
}

/**
 * Box utils
 */

static inline bool is_empty(Box *box) {
    return !box->exists;
}

static inline bool has_dropped(Box *box) {
    return box->offset == 0;
}

static Box *get_upper_box(Field field, Position current) {
    return (&field[current.y - 1][current.x]);
}

static Box *get_lower_box(Field field, Position current) {
    return (&field[current.y + 1][current.x]);
}

static Box *get_next_box(Field field, Position current, int x_direction) {
    return (&field[current.y][current.x + x_direction]);
}

static inline void decrement_y_offset_to_zero(Box *n) {
    if (n->offset) --n->offset;
}

static inline void heap_swap(Box* first, Box* second) {
    Box temp = *first;

    *first = *second;
    *second = temp;
}

/**
 * #Box logic
 */

static void generate_box(GameState const* game) {
    furi_assert(game);

    static byte tick_count = 0;
    if (tick_count++ != BOX_GENERATION_RATE) {
        return;
    }
    tick_count = 0;

    int x_offset = rand() % X_FIELD_SIZE;
    while (game->field[0][x_offset].exists) {
        x_offset = rand() % X_FIELD_SIZE;
    }

    game->field[0][x_offset].exists = true;
    game->field[0][x_offset].offset = BOX_HEIGHT;
    game->field[0][x_offset].box_id = rand() % 5;
}

static void drop_box(GameState* game) {
    furi_assert(game);

    for (int y = Y_LAST; y > 0; y--) {
        for (int x = 0; x < X_FIELD_SIZE; x++) {
            Box* current_box = game->field[y] + x;
            Box* upper_box = game->field[y - 1] + x;

            if (y == Y_LAST) {
				decrement_y_offset_to_zero(current_box);
            }

            decrement_y_offset_to_zero(upper_box);

            if (is_empty(current_box) && !is_empty(upper_box) && has_dropped(upper_box)) {
                upper_box->offset = BOX_HEIGHT;
                heap_swap(current_box, upper_box);
            }
        }
    }
}

static void clear_rows(Box** field) {
    for (int x = 0; x < X_FIELD_SIZE; ++x) {
        if (is_empty(field[Y_LAST] + x) || !has_dropped(field[Y_LAST] + x)) {
            return;
        }
    }
    memset(field[Y_LAST], 0, sizeof(Box) * X_FIELD_SIZE);
    clear_rows(field);
}

/**
 * Input Handling
 */

static void handle_key_presses(Person* person, InputEvent* input, GameState *game) {
    switch(input->key) {
        case InputKeyUp:
            if (person->j_tick == 0)
                person->j_tick = 1;
            break;
        case InputKeyLeft:
            person->right_frame = false;
            if(person->h_tick == 0) {
                person->h_tick = 1;
                person->x_direction = -1;
            }
            break;
        case InputKeyRight:
            person->right_frame = true;
            if(person->h_tick == 0) {
                person->h_tick = 1;
                person->x_direction = 1;
            }
            break;
        case InputKeyBack:
            game->game_status = GameStatusExit;
            break;
        default:
            game->game_status = GameStatusPause;
            game->animation = AnimationPause;
        }
}

/**
 * #Person logic
 */

static inline bool ground_box_check(Field field, Position new_position) {
    Box *lower_box = get_lower_box(field, new_position);

    bool ground_box_dropped = (new_position.y == Y_LAST || //Eсли мы и так в самом низу
                              is_empty(lower_box) || // Ecли снизу пустота
                              has_dropped(lower_box)); //Eсли бокс снизу допадал
    return ground_box_dropped;
}

static bool is_movable(Field field, Position box_pos, int x_direction) {
    //TODO::Moжет и не двух, предположение
    bool box_on_top = box_pos.y < 2 || get_upper_box(field, box_pos)->exists;
    bool has_next_box = get_next_box(field, box_pos, x_direction)->exists;

    return (!box_on_top && !has_next_box);
}

static bool horizontal_move(Person* person, Field field) {
    Position new_position = person->p;

    if (!person->x_direction) return false;

    new_position.x += person->x_direction;

    bool on_edge_column = new_position.x < 0 || new_position.x > X_LAST;
    if (on_edge_column) return false;

    if (is_empty(&field[new_position.y][new_position.x])) {
        bool ground_box_dropped = ground_box_check(field, new_position);
        if (ground_box_dropped) {
            person->p = new_position;
            return true;
        }
    } else if (is_movable(field, new_position, person->x_direction)) {
        *get_next_box(field, new_position, person->x_direction) =
            field[new_position.y][new_position.x];

        field[new_position.y][new_position.x].exists = false;
        person->p = new_position;
        return true;
    }
    return false;
}


static inline bool on_ground(Person* person, Field field){
    return person->p.y == Y_LAST || field[person->p.y + 1][person->p.x].exists;
}

static void person_move(Person* person, Field field) {
    /// Left-right logic
    FURI_LOG_W(TAG, "[JUMP]func:[%s] line: %d", __FUNCTION__, __LINE__);
    switch(person->h_tick) {
        case 0:
            break;
        case 1:
            person->h_tick++;
            FURI_LOG_W(TAG, "[JUMP]func:[%s] line: %d", __FUNCTION__, __LINE__);
            bool moved = horizontal_move(person, field);
            if(!moved) {
                person->h_tick = 0;
                person->x_direction = 0;
            }
            break;
        case 5:
            FURI_LOG_W(TAG, "[JUMP]func:[%s] line: %d", __FUNCTION__, __LINE__);
            person->h_tick = 0;
            person->x_direction = 0;
            break;
        default:
            FURI_LOG_W(TAG, "[JUMP]func:[%s] line: %d", __FUNCTION__, __LINE__);
            person->h_tick++;
    }

    switch(person->j_tick) {
        case 0:
            if (!on_ground(person, field)) {
                person->p.y++;
                person->j_tick--;
            }
            break;
        case 1:
            if (on_ground(person, field)) {
                person->p.y--;
                person->j_tick++;
            }
            break;
        case 6:
            person->j_tick = 0;
            break;
        case -6:
            person->j_tick = 0;
            break;
        default:
            person->j_tick += person->j_tick > 0 ? 1 : -1;
    }
    if (person->j_tick > 0) {
        field[person->p.y][person->p.x] = (Box){0};
        *get_upper_box(field, person->p) = (Box){0};
    }
}

static inline bool is_person_dead(Person* person, Box** field) {
    return get_upper_box(field, person->p)->exists;
}

/**
 * #Callback
 */

static void draw_box(Canvas* canvas, Box* box, int x, int y) {
    if (is_empty(box)) {
        return;
    }
    byte y_screen = y * BOX_HEIGHT - box->offset;
    byte x_screen = x * BOX_WIDTH + 4;

    canvas_draw_icon(canvas, x_screen, y_screen, boxes[box->box_id]);
}

static void heap_defense_render_callback(Canvas* const canvas, void* mutex) {
    furi_assert(mutex);

    const GameState* game = acquire_mutex((ValueMutex*)mutex, 25);

    ///Draw GameOver or Pause
    if (game->game_status != GameStatusInProgress) {
        FURI_LOG_W(TAG, "[DAED_DRAW]func: [%s] line: %d ", __FUNCTION__, __LINE__);

        canvas_draw_icon_animation(canvas, 0,0, animations[game->animation]);
        release_mutex((ValueMutex*)mutex, game);
        return;
    }

    ///Draw field
    canvas_draw_icon(canvas, 0,0, &I_Background_128x64);

    ///Draw Person
    const Person* person = game->person;
    IconAnimation* player_animation = NULL;

    uint8_t x_screen = person->p.x * BOX_WIDTH + DRAW_X_OFFSET;
    if (person->h_tick && person->h_tick != 1) {

        if (person->right_frame) {
            x_screen += (person->h_tick) * 2 - BOX_WIDTH;
            player_animation = animations[AnimationRight];
        } else {
            x_screen -= (person->h_tick) * 2 - BOX_WIDTH;
            player_animation = animations[AnimationLeft];
        }

    }

    uint8_t y_screen = (person->p.y - 1) * BOX_HEIGHT;
    if (person->j_tick) {

        if (person->j_tick > 1) {
            y_screen += BOX_HEIGHT - (person->j_tick) * 2;
        } else {
            y_screen -= BOX_HEIGHT + (person->j_tick) * 2;
        }

    }

    if (player_animation) {
        canvas_draw_icon_animation(canvas, x_screen, y_screen, player_animation);
    } else {
        canvas_draw_icon(canvas, x_screen, y_screen, &I_PersonStand_20x10);
    }

    ///Draw Boxes
    canvas_set_color(canvas, ColorBlack);
    for (int y = 0; y < Y_FIELD_SIZE; ++y) {
        for (int x = 0; x < X_FIELD_SIZE; ++x) {
            draw_box(canvas, &(game->field[y][x]), x, y);
        }
    }

    release_mutex((ValueMutex*)mutex, game);
}

static void heap_defense_input_callback(InputEvent* input_event, osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    GameEvent event = { .type = EventKeyPress, .input = *input_event };
    osMessageQueuePut(event_queue, &event, 0, osWaitForever);
}

static void heap_defense_timer_callback(osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    GameEvent event = { .type = EventGameTick, .input = {0} };
    event.type = EventGameTick;
    osMessageQueuePut(event_queue, &event, 0, 0);
}

int32_t heap_defence_app(void* p) {
    srand(DWT->CYCCNT);

    FURI_LOG_W(TAG, "Heap defence start %d", __LINE__);
    osMessageQueueId_t event_queue = osMessageQueueNew(8, sizeof(GameEvent), NULL);
    GameState* game = allocGameState();

    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, game, sizeof(GameState))) {
        game_destroy(game);
        return 1;
    }

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, heap_defense_render_callback, &state_mutex);
    view_port_input_callback_set(view_port, heap_defense_input_callback, event_queue);

    osTimerId_t timer =
        osTimerNew(heap_defense_timer_callback, osTimerPeriodic, event_queue, NULL);
    osTimerStart(timer, osKernelGetTickFreq() / TIMER_UPDATE_FREQ);

    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    ///Animation init!
	animations_alloc_and_start();
    GameEvent event = {0};
    while (event.input.key != InputKeyBack) {

        if (osOK != osMessageQueueGet(event_queue, &event, 0, 100)) {
            continue;
        }

        game = (GameState*)acquire_mutex_block(&state_mutex);

        if (game->game_status != GameStatusInProgress) {

            if (event.type == EventKeyPress && event.input.key == InputKeyOk) {
                game->game_status = GameStatusInProgress;
            }

        } else if (event.type == EventGameTick) {

            if (is_person_dead(game->person, game->field)) {
                game->game_status = GameStatusOver;
                game->animation = AnimationGameOver;
                game_reset_field_and_player(game);
            } else {
                drop_box(game);
                generate_box(game);
                clear_rows(game->field);
                person_move(game->person, game->field);
            }

        } else if (event.type == EventKeyPress) {
            handle_key_presses(game->person, &(event.input), game);
        }

        release_mutex(&state_mutex, game);
        view_port_update(view_port);
    }

    osTimerDelete(timer);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close("gui");
    osMessageQueueDelete(event_queue);
    animations_stop_and_destroy();
    delete_mutex(&state_mutex);
    game_destroy(game);

    return 0;
}

