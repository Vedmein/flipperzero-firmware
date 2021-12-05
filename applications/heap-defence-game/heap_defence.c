//
// Created by moh on 30.11.2021.
//

#include <stdlib.h>
#include <string.h>
//#include <stdlib.h>

#include "../flipperzero-firmware/core/furi.h"
#include "../flipperzero-firmware/applications/gui/gui.h"
#include "../flipperzero-firmware/applications/input/input.h"
#include "../flipperzero-firmware/lib/STM32CubeWB/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.h"
#include "../flipperzero-firmware/applications/input/input.h"
#include "../flipperzero-firmware/firmware/targets/f7/furi-hal/furi-hal-resources.h"
#include <stdlib.h>
#include <string.h>

#define Y_FIELD_SIZE 6
#define Y_LAST (Y_FIELD_SIZE - 1)
#define X_FIELD_SIZE 12
#define X_LAST (X_FIELD_SIZE - 1)

#define TAG "HeDe"

#define BOX_HEIGHT 10
#define BOX_WIDTH 10
#define TIMER_UPDATE_FREQ 6
#define BOX_DROP_RATE 5
#define BOX_GENERATION_RATE 70

static int tick_count = 0;

#define PERSON_HEIGHT (BOX_HEIGHT * 2) // TODO Каждый раз будет заново считать
#define PERSON_WIDTH BOX_WIDTH
#define PERSON_STEP_PIX 1
#define PERSON_RIGHT (person->screen_x + PERSON_WIDTH)
#define PERSON_LEFT (person->screen_x)
#define PERSON_BOTTOM (person->screen_y - PERSON_HEIGHT)
#define PERSON_TOP (person->screen_y)

typedef u_int8_t byte;

typedef enum {
    StatusWaitingForStart,
    StatusGameInProgress,
    StatusGameOver,
    StatusGameExit
} GameStatuses;

typedef enum {
    PersonStatusDead,
    PersonStatusWalk,
    PersonStatusJump,
    PersonStatusPush
} PersonStatuses;

typedef struct {
    int x;
    int y;
} Position;

typedef struct {
    int x_direction;
    byte do_jump;
    byte is_walking;
    Position p;
    PersonStatuses status;
} Person;

typedef struct {
    byte type;
    byte state;
    byte shift;
    byte offset;
} Box;

typedef struct {
    Box** field;
    Person* person;
    GameStatuses game_status;
//    byte tick_count;
} GameState;

typedef Box** Field;

typedef enum { EventKeyPress, EventGameTick } EventType;

typedef struct {
    EventType type;
    InputEvent input;
} GameEvent;

/**
 * #Construct / Destroy
 */
GameState* allocGameState() {
    GameState* game_state = furi_alloc(sizeof(GameState));
    game_state->person = furi_alloc(sizeof(Person));
    game_state->field = furi_alloc(Y_FIELD_SIZE * sizeof(Box*));
    for(int y = 0; y < Y_FIELD_SIZE; ++y) {
        game_state->field[y] = furi_alloc(X_FIELD_SIZE * sizeof(Box));
    }
    game_state->person->p.x = 5;
    game_state->person->p.y = Y_LAST;
    game_state->game_status = StatusWaitingForStart;
    return game_state;
}

void game_state_destroy(GameState* game_state) {
    for(int y = 0; y < Y_FIELD_SIZE; ++y) {
        free(game_state->field[y]);
    }
    free(game_state->person);
    free(game_state->field);
    free(game_state);
}

/**
 * #Box logic
 */

static void generate_box(GameState const* game_state) {
    furi_assert(game_state);
    if(tick_count % BOX_GENERATION_RATE) {
        return;
    }

    int x_offset = rand() % X_FIELD_SIZE;
    game_state->field[0][x_offset].state = 1;
    game_state->field[0][x_offset].offset = BOX_HEIGHT;
}

static void heap_swap(Box* first, Box* second) {
    Box temp = *first;
    first->state = second->state;
    second->type = temp.type; //чтобы не потерять текстуру
    *first = *second;
    *second = temp;
}
void dec_offset(byte* p_offset) {
    if(*p_offset) (*p_offset)--;
}

static void drop_box(GameState* game_state) {
    furi_assert(game_state);

    for(int y = Y_LAST; y > 0; y--) {
        for(int x = 0; x < X_FIELD_SIZE; x++) {
            Box* cur_cell = game_state->field[y] + x;
            Box* upper_cell = game_state->field[y - 1] + x;

            if(y == Y_LAST) {
                dec_offset(&(cur_cell->offset));
            }

            byte* offset = &(upper_cell->offset);
            dec_offset(offset);

            if(cur_cell->state == 0 && (upper_cell->state != 0 && *offset == 0)) {
                *offset = BOX_HEIGHT;
                heap_swap(cur_cell, upper_cell); //TODO: Think about
            }
        }
    }
}

static void clear_rows(Box** field) {
    int bottom_row = Y_FIELD_SIZE - 1;
    for(int x = 0; x < X_FIELD_SIZE; ++x) {
        if(field[bottom_row][x].state == 0) {
            return;
        }
    }
    memset(field[bottom_row], 0, sizeof(Box) * X_FIELD_SIZE);
    clear_rows(field);
}

/**
 * #Person logic
 */

static void person_set_events(Person* person, InputEvent* input) {
    //    if (person->status == PersonStatusJump || person->status == PersonStatusPush) {
    //        return;
    //    }
    switch(input->key) {
    case InputKeyUp:
        //TODO: может лучше будет выставлять когда персонаж не идет
        person->do_jump = 1;
        break;
    case InputKeyLeft:
        FURI_LOG_W(TAG, "func: INPUT_EVENT_LEFT");
        if(!person->x_direction) {
            person->x_direction = -1;
        }
        break;
    case InputKeyRight:
        if(!person->x_direction) person->x_direction = 1;
        break;
    default:
        break;
    }
}

static bool is_moveble(Position box_pos, int x_direction, Field field) {


    bool on_edge_col = box_pos.x == 0 || box_pos.x == X_LAST;
    //TODO::Moжет и не двух, предположение
    bool box_on_top = box_pos.y < 2 || field[box_pos.y - 1][box_pos.x].state != 0;

    FURI_LOG_W(TAG, "func: [%s] line: %d on_ege: %d on_top: %d", __FUNCTION__, __LINE__, on_edge_col, box_on_top);
    if(box_on_top || on_edge_col) return false;

    bool has_next_box = field[box_pos.y][box_pos.x + x_direction].state != 0;

    FURI_LOG_W(TAG, "func: [%s] line: %d has_next: %d", __FUNCTION__, __LINE__, has_next_box);
    if(has_next_box) return false;

    return true;
}

static void horizontal_move(Person* person, Field field) {
    Position new_position = person->p;

    //TODO:Check ground?
    if(!person->x_direction) return;

    FURI_LOG_W(TAG, "func: %s line: %d direction: %d", __FUNCTION__, __LINE__, person->x_direction);

    new_position.x += person->x_direction;

    bool on_edge_position = new_position.x < 0 || new_position.x > X_LAST;
    if(on_edge_position) return;


    FURI_LOG_W(
        TAG, "pre_move: func: %s line: %d x:%d y:%d", __FUNCTION__, __LINE__, person->p.x, person->p.y);

    if(field[new_position.y][new_position.x].state == 0) {
        person->p = new_position;
    } else if(is_moveble(new_position, person->x_direction, field)) {
        //TODO:animation

        FURI_LOG_W(TAG, "move:func: %s line: %d x:%d y:%d",
                   __FUNCTION__, __LINE__, person->p.x, person->p.y);
        field[new_position.y][new_position.x + person->x_direction] =
            field[new_position.y][new_position.x];

        field[new_position.y][new_position.x].state = 0;
        person->p = new_position;
    }

    FURI_LOG_W(
        TAG, "func: %s line: %d x:%d y:%d", __FUNCTION__, __LINE__, person->p.x, person->p.y);
}

static inline bool on_ground(Person* person, Field field){
    return person->p.y == Y_LAST || field[person->p.y + 1][person->p.x].state != 0;
}

static void jump_move(Person* person, Field field) {
   if(!on_ground(person, field))
       return;
   person->p.y--;
}

static void person_move(Person* person, Field field) {

    /// Left-right logic
    if(person->x_direction) {
        FURI_LOG_W(TAG, "[MOVE]func:[%s] line: %d", __FUNCTION__, __LINE__);
        horizontal_move(person, field)+;
        person->x_direction = 0;
    }


    ///Jump logic
    if(person->do_jump) {
        FURI_LOG_W(TAG, "[JUMP]func:[%s] line: %d", __FUNCTION__, __LINE__);
        jump_move(person, field);
        person->do_jump = 0;
    }


}


/**
 * #Callback
 */

static void draw_box(Canvas* canvas, Box* box, int x, int y) {
    if(!box->state) {
        return;
    }
    byte y_screen = y * BOX_WIDTH - box->offset;
    byte x_screen = x * BOX_HEIGHT;

    canvas_draw_frame(canvas, x_screen, y_screen, BOX_WIDTH, BOX_HEIGHT);
}

static void heap_defense_render_callback(Canvas* const canvas, void* mutex) {
    int timeout = 25;
    const GameState* game_state = acquire_mutex((ValueMutex*)mutex, timeout);
    if(!game_state) return;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    for(int y = 0; y < Y_FIELD_SIZE; ++y) {
        for(int x = 0; x < X_FIELD_SIZE; ++x) {
            draw_box(canvas, &(game_state->field[y][x]), x, y);
        }
    }

    canvas_draw_frame(
        canvas,
        game_state->person->p.x * BOX_WIDTH,
        (game_state->person->p.y - 1) * BOX_HEIGHT,
        PERSON_WIDTH,
        PERSON_HEIGHT);
    release_mutex((ValueMutex*)mutex, game_state);
}

static void heap_defense_input_callback(InputEvent* input_event, osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    GameEvent event;
    event.input = *input_event;
    event.type = EventKeyPress;
    osMessageQueuePut(event_queue, &event, 0, osWaitForever);
}

static void heap_defense_timer_callback(osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    GameEvent event;
    event.type = EventGameTick;
    osMessageQueuePut(event_queue, &event, 0, 0);
}

int32_t heap_defence_app(void* p) {
    srand(DWT->CYCCNT);

    FURI_LOG_W(TAG, "Heap defence start %s", "hi");
    FURI_LOG_W(TAG, "Heap defence start %d", __LINE__);
    osMessageQueueId_t event_queue = osMessageQueueNew(8, sizeof(GameEvent), NULL);
    GameState* game = allocGameState();

    ValueMutex state_mutex;
    if(!init_mutex(&state_mutex, game, sizeof(GameState))) {
        free(game);
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

    GameEvent event = {.type = 0, .input = {0}};
    while(event.input.key != InputKeyBack) { /// ATTENTION
        if(osMessageQueueGet(event_queue, &event, NULL, 100) != osOK) {
            continue;
        }
        GameState* game_state = (GameState*)acquire_mutex_block(&state_mutex);
        if(event.type == EventKeyPress) {
            person_set_events(game_state->person, &(event.input));
        } else if(event.type == EventGameTick) {
            drop_box(game_state);
            //TODO:заглушка, нужно сделать синхронизацию
            ///Person_drop
            if(!(tick_count % 15)) {
               if(!on_ground(game_state->person, game_state->field)) {
                   game_state->person->p.y++;
               }
            }

            generate_box(game_state);
            clear_rows(game_state->field);
            tick_count++;

            if(!(tick_count % 3)) { //TODO: передвижение должно занимать Н тиков пока не реализовано(
                person_move(game_state->person, game_state->field);
            }
        }
        release_mutex(&state_mutex, game_state);
        view_port_update(view_port);
    }

    osTimerDelete(timer);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    osMessageQueueDelete(event_queue);
    delete_mutex(&state_mutex);
    game_state_destroy(game);

    return 0;
}
