#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "buzzer.h"
#include "display.h"
#include "display_macros.h"
#include "initialisation.h"
#include "timer.h"
#include "uart.h"

#ifndef STUDENT_NUMBER_SEED
#define STUDENT_NUMBER_SEED 0x01234567UL
#endif

#ifndef STUDENT_NUMBER_XY
#define STUDENT_NUMBER_XY 67U
#endif

#define LFSR_MASK 0xE2025CABUL
#define MAX_SEQUENCE_LENGTH 99U
#define INPUT_QUEUE_SIZE 16U
#define HIGH_SCORE_LIMIT 5U
#define HIGH_SCORE_NAME_LENGTH 20U
#define BUTTON_MASK 0x0FU
#define BUTTON_DEBOUNCE_MS 5U
#define PLAYBACK_MIN_DELAY_MS 250U
#define PLAYBACK_MAX_DELAY_MS 2000U
#define HIGH_SCORE_TIMEOUT_MS 5000UL

typedef enum
{
    INPUT_SOURCE_BUTTON = 0,
    INPUT_SOURCE_UART = 1
} input_source_t;

typedef struct
{
    uint8_t step;
    input_source_t source;
} input_event_t;

typedef struct
{
    bool valid;
    uint16_t score;
    char name[HIGH_SCORE_NAME_LENGTH + 1];
} high_score_entry_t;

typedef struct
{
    bool collecting;
    uint8_t count;
    uint32_t value;
    bool invalid;
} seed_state_t;

typedef struct
{
    bool active;
    uint16_t score;
    char buffer[HIGH_SCORE_NAME_LENGTH + 1];
    uint8_t length;
    uint32_t prompt_time;
    uint32_t last_input_time;
} high_score_prompt_t;

static void state_machine(void);
static void service_tasks(void);
static void reset_current_game(void);
static void setup_new_game(void);
static uint8_t lfsr_next_step(uint32_t *state);
static void ensure_steps(uint16_t target_index);
static uint32_t rewind_state_to(uint16_t index);
static uint16_t compute_playback_delay(void);
static void simon_playback(void);
static bool player_turn(void);
static void display_sequence_step(uint8_t step);
static bool dequeue_input(input_event_t *event);
static void enqueue_input(uint8_t step, input_source_t source);
static void clear_input_queue(void);
static void update_frequencies(int8_t delta);
static void apply_frequency_shift(int8_t delta);
static uint16_t base_frequency_from_digits(void);
static void handle_success(uint16_t score);
static void handle_failure(uint16_t score);
static void transmit_success(uint16_t score);
static void transmit_game_over(uint16_t score);
static void buttons_update(void);
static bool button_is_pressed(uint8_t index);
static void process_uart_byte(uint8_t byte);
static void seed_reset(void);
static void high_score_prompt_begin(uint16_t score);
static void high_score_prompt_process_char(char c);
static void high_score_prompt_update(void);
static void high_scores_add(const char *name, uint16_t score);
static bool high_scores_qualifies(uint16_t score);
static void high_scores_transmit(void);
static void delay_with_abort(uint16_t ms);

int main(void)
{
    cli();
    system_initialise();
    timer_register_service(service_tasks);
    sei();

    state_machine();

    while (1)
        ;
}

static volatile bool reset_requested = false;

static uint32_t default_seed = STUDENT_NUMBER_SEED;
static uint32_t active_seed = STUDENT_NUMBER_SEED;
static uint32_t lfsr_state_current = STUDENT_NUMBER_SEED;
static uint16_t max_generated_index = 0;
static uint16_t sequence_start_index = 1;
static uint16_t sequence_length = 1;
static uint32_t pending_seed = 0;
static bool has_pending_seed = false;

static uint8_t input_head = 0;
static uint8_t input_tail = 0;
static input_event_t input_queue[INPUT_QUEUE_SIZE];

static uint8_t button_raw_state = 0;
static uint8_t button_debounced_state = 0;
static uint8_t button_last_report = 0;
static uint32_t button_last_change = 0;

static uint16_t base_frequencies[4];
static uint16_t current_frequencies[4];
static int8_t octave_shift = 0;

static high_score_entry_t high_scores[HIGH_SCORE_LIMIT];
static uint8_t high_score_count = 0;
static high_score_prompt_t high_score_prompt = {0};
static seed_state_t seed_state = {0};

static const uint8_t step_display_patterns[4][2] = {
    {DISP_BAR_LEFT, DISP_OFF},
    {DISP_BAR_RIGHT, DISP_OFF},
    {DISP_OFF, DISP_BAR_LEFT},
    {DISP_OFF, DISP_BAR_RIGHT},
};

static const uint32_t ratio_e_high = 749154UL;
static const uint32_t ratio_c_sharp = 594604UL;
static const uint32_t ratio_e_low = 374577UL;
static const uint32_t ratio_scale = 1000000UL;

static uint16_t base_frequency_from_digits(void)
{
    uint8_t digits = (uint8_t)(STUDENT_NUMBER_XY % 100U);
    uint8_t tens = (uint8_t)(digits / 10U);
    uint8_t ones = (uint8_t)(digits % 10U);
    return (uint16_t)(400U + (uint16_t)(tens * 10U) + ones);
}

static uint16_t apply_ratio(uint16_t base, uint32_t ratio)
{
    uint32_t scaled = (uint32_t)base * ratio + (ratio_scale / 2UL);
    return (uint16_t)(scaled / ratio_scale);
}

static void initialise_frequencies(void)
{
    uint16_t base = base_frequency_from_digits();
    base_frequencies[0] = apply_ratio(base, ratio_e_high);
    base_frequencies[1] = apply_ratio(base, ratio_c_sharp);
    base_frequencies[2] = base;
    base_frequencies[3] = apply_ratio(base, ratio_e_low);
    for (uint8_t i = 0; i < 4; ++i)
    {
        current_frequencies[i] = base_frequencies[i];
    }
    octave_shift = 0;
}

static void state_machine(void)
{
    initialise_frequencies();
    setup_new_game();

    while (1)
    {
        reset_requested = false;
        clear_input_queue();
        simon_playback();
        if (reset_requested)
        {
            reset_current_game();
            continue;
        }

        bool success = player_turn();
        if (reset_requested)
        {
            reset_current_game();
            continue;
        }

        if (success)
        {
            uint16_t score = sequence_length;
            handle_success(score);
            if (sequence_length >= MAX_SEQUENCE_LENGTH)
            {
                uint16_t end_index = sequence_start_index + sequence_length - 1U;
                sequence_start_index = end_index + 1U;
                sequence_length = 1U;
                setup_new_game();
                continue;
            }

            ++sequence_length;
            ensure_steps(sequence_start_index + sequence_length - 1U);
        }
        else
        {
            uint16_t score = sequence_length;
            handle_failure(score);
            uint16_t end_index = sequence_start_index + sequence_length - 1U;
            sequence_start_index = end_index;
            sequence_length = 1U;
            setup_new_game();
        }
    }
}

static void reset_current_game(void)
{
    buzzer_stop();
    display_blank();
    seed_reset();
    initialise_frequencies();
    reset_requested = false;
    setup_new_game();
}

static void seed_reset(void)
{
    seed_state.collecting = false;
    seed_state.invalid = false;
    seed_state.count = 0;
    active_seed = default_seed;
    lfsr_state_current = active_seed;
    max_generated_index = 0;
    sequence_start_index = 1;
    sequence_length = 1;
}

static void setup_new_game(void)
{
    if (high_score_prompt.active)
    {
        high_score_prompt.active = false;
    }

    if (has_pending_seed)
    {
        active_seed = pending_seed;
        lfsr_state_current = pending_seed;
        max_generated_index = 0;
        has_pending_seed = false;
    }

    clear_input_queue();
    ensure_steps(sequence_start_index + sequence_length - 1U);
    display_blank();
}

static void ensure_steps(uint16_t target_index)
{
    while (max_generated_index < target_index)
    {
        (void)lfsr_next_step(&lfsr_state_current);
        ++max_generated_index;
    }
}

static uint8_t lfsr_next_step(uint32_t *state)
{
    uint8_t bit = (uint8_t)(*state & 1UL);
    *state >>= 1;
    if (bit)
    {
        *state ^= LFSR_MASK;
    }
    return (uint8_t)(*state & 0x03UL);
}

static uint32_t rewind_state_to(uint16_t index)
{
    uint32_t state = active_seed;
    for (uint16_t i = 1; i < index; ++i)
    {
        (void)lfsr_next_step(&state);
    }
    return state;
}

static uint16_t compute_playback_delay(void)
{
    ADMUX = (ADMUX & 0xF0) | 0x00;
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC))
        ;
    uint16_t value = ADC;
    uint32_t scaled = PLAYBACK_MIN_DELAY_MS + ((uint32_t)(PLAYBACK_MAX_DELAY_MS - PLAYBACK_MIN_DELAY_MS) * value) / 1023UL;
    if (scaled < PLAYBACK_MIN_DELAY_MS)
        scaled = PLAYBACK_MIN_DELAY_MS;
    if (scaled > PLAYBACK_MAX_DELAY_MS)
        scaled = PLAYBACK_MAX_DELAY_MS;
    return (uint16_t)scaled;
}

static void simon_playback(void)
{
    uint32_t state = rewind_state_to(sequence_start_index);
    for (uint16_t i = 0; i < sequence_length; ++i)
    {
        uint8_t step = lfsr_next_step(&state);
        uint16_t delay_ms = compute_playback_delay();
        uint16_t half = delay_ms / 2U;
        uint16_t remainder = delay_ms - half;

        display_sequence_step(step);
        uint16_t freq = current_frequencies[step];
        uint16_t applied_freq = 0;
        if (freq < 20U)
            freq = 20U;
        buzzer_start(freq);
        applied_freq = freq;

        uint32_t start = timer_millis();
        while ((uint32_t)(timer_millis() - start) < half)
        {
            if (reset_requested)
                break;
            if (current_frequencies[step] != applied_freq)
            {
                applied_freq = current_frequencies[step];
                if (applied_freq < 20U)
                    applied_freq = 20U;
                buzzer_update(applied_freq);
            }
            timer_service();
        }

        buzzer_stop();
        display_blank();
        if (reset_requested)
            break;
        delay_with_abort(remainder);
        if (reset_requested)
            break;
    }
}

static bool dequeue_input(input_event_t *event)
{
    if (input_head == input_tail)
        return false;
    *event = input_queue[input_tail];
    input_tail = (uint8_t)((input_tail + 1U) % INPUT_QUEUE_SIZE);
    return true;
}

static void enqueue_input(uint8_t step, input_source_t source)
{
    uint8_t next = (uint8_t)((input_head + 1U) % INPUT_QUEUE_SIZE);
    if (next == input_tail)
        return;
    input_queue[input_head].step = step;
    input_queue[input_head].source = source;
    input_head = next;
}

static void clear_input_queue(void)
{
    input_head = 0;
    input_tail = 0;
}

static void display_sequence_step(uint8_t step)
{
    if (step < 4U)
    {
        display_set_raw(step_display_patterns[step][0], step_display_patterns[step][1]);
    }
}

static bool button_is_pressed(uint8_t index)
{
    return (button_debounced_state & (1U << index)) != 0U;
}

static bool player_turn(void)
{
    clear_input_queue();
    uint32_t state = rewind_state_to(sequence_start_index);
    for (uint16_t i = 0; i < sequence_length; ++i)
    {
        uint8_t expected = lfsr_next_step(&state);
        input_event_t event;
        while (!dequeue_input(&event))
        {
            if (reset_requested)
                return false;
            timer_service();
        }

        uint16_t delay_ms = compute_playback_delay();
        uint16_t half = delay_ms / 2U;
        uint16_t remainder = delay_ms - half;

        display_sequence_step(event.step);
        uint16_t freq = current_frequencies[event.step];
        if (freq < 20U)
            freq = 20U;
        buzzer_start(freq);
        uint16_t applied_freq = freq;
        uint32_t start = timer_millis();
        while ((uint32_t)(timer_millis() - start) < half)
        {
            if (reset_requested)
                break;
            if (current_frequencies[event.step] != applied_freq)
            {
                applied_freq = current_frequencies[event.step];
                if (applied_freq < 20U)
                    applied_freq = 20U;
                buzzer_update(applied_freq);
            }
            timer_service();
        }

        if (event.source == INPUT_SOURCE_BUTTON)
        {
            while (button_is_pressed(event.step))
            {
                if (reset_requested)
                    break;
                if (current_frequencies[event.step] != applied_freq)
                {
                    applied_freq = current_frequencies[event.step];
                    if (applied_freq < 20U)
                        applied_freq = 20U;
                    buzzer_update(applied_freq);
                }
                timer_service();
            }
        }

        buzzer_stop();
        display_blank();
        if (reset_requested)
            return false;
        delay_with_abort(remainder);
        if (reset_requested)
            return false;

        if (event.step != expected)
        {
            return false;
        }
    }
    return true;
}

static void handle_success(uint16_t score)
{
    transmit_success(score);
    display_show_success();
    uint16_t delay_ms = compute_playback_delay();
    delay_with_abort(delay_ms);
    display_blank();
}

static void handle_failure(uint16_t score)
{
    transmit_game_over(score);
    display_show_fail();
    uint16_t delay_ms = compute_playback_delay();
    delay_with_abort(delay_ms);
    display_show_number(score);
    delay_with_abort(delay_ms);
    display_blank();
    delay_with_abort(delay_ms);

    if (high_scores_qualifies(score))
    {
        high_score_prompt_begin(score);
        while (high_score_prompt.active)
        {
            timer_service();
        }
    }
}

static void transmit_success(uint16_t score)
{
    uart_send_string("SUCCESS\n");
    char buffer[8];
    uint16_t temp = score;
    uint8_t index = 0;
    if (temp == 0)
    {
        buffer[index++] = '0';
    }
    else
    {
        char digits[6];
        uint8_t count = 0;
        while (temp > 0 && count < sizeof digits)
        {
            digits[count++] = (char)('0' + (temp % 10U));
            temp /= 10U;
        }
        while (count > 0)
        {
            buffer[index++] = digits[--count];
        }
    }
    buffer[index] = '\0';
    uart_send_string(buffer);
    uart_send_char('\n');
}

static void transmit_game_over(uint16_t score)
{
    uart_send_string("GAME OVER\n");
    char buffer[8];
    uint16_t temp = score;
    uint8_t index = 0;
    if (temp == 0)
    {
        buffer[index++] = '0';
    }
    else
    {
        char digits[6];
        uint8_t count = 0;
        while (temp > 0 && count < sizeof digits)
        {
            digits[count++] = (char)('0' + (temp % 10U));
            temp /= 10U;
        }
        while (count > 0)
        {
            buffer[index++] = digits[--count];
        }
    }
    buffer[index] = '\0';
    uart_send_string(buffer);
    uart_send_char('\n');
}

static void service_tasks(void)
{
    buttons_update();
    while (uart_rx_available())
    {
        uint8_t byte = uart_read_byte();
        process_uart_byte(byte);
    }
    high_score_prompt_update();
}

static void buttons_update(void)
{
    uint8_t raw = (uint8_t)(~PINB) & BUTTON_MASK;
    if (raw != button_raw_state)
    {
        button_raw_state = raw;
        button_last_change = timer_millis();
    }
    else if ((uint32_t)(timer_millis() - button_last_change) >= BUTTON_DEBOUNCE_MS)
    {
        button_debounced_state = button_raw_state;
    }

    uint8_t presses = (uint8_t)(button_debounced_state & (uint8_t)~button_last_report);
    if (presses != 0U)
    {
        for (uint8_t idx = 0; idx < 4U; ++idx)
        {
            if ((presses & (1U << idx)) != 0U)
            {
                enqueue_input(idx, INPUT_SOURCE_BUTTON);
            }
        }
    }
    button_last_report = button_debounced_state;
}

static void update_frequencies(int8_t delta)
{
    if (delta == 0)
        return;
    apply_frequency_shift(delta);
}

static bool frequencies_can_shift(int8_t delta)
{
    if (delta == 0)
        return true;
    for (uint8_t i = 0; i < 4U; ++i)
    {
        uint32_t value = current_frequencies[i];
        if (delta > 0)
        {
            value <<= delta;
        }
        else
        {
            value >>= (uint8_t)(-delta);
        }
        if (value < 20UL || value > 20000UL)
        {
            return false;
        }
    }
    return true;
}

static void apply_frequency_shift(int8_t delta)
{
    if (!frequencies_can_shift(delta))
        return;
    octave_shift = (int8_t)(octave_shift + delta);
    for (uint8_t i = 0; i < 4U; ++i)
    {
        uint32_t value = base_frequencies[i];
        if (octave_shift > 0)
        {
            for (int8_t j = 0; j < octave_shift; ++j)
            {
                value <<= 1;
                if (value > 20000UL)
                {
                    value = 20000UL;
                    break;
                }
            }
        }
        else if (octave_shift < 0)
        {
            for (int8_t j = 0; j < -octave_shift; ++j)
            {
                value >>= 1;
                if (value < 20UL)
                {
                    value = 20UL;
                    break;
                }
            }
        }
        current_frequencies[i] = (uint16_t)value;
    }
}

static void process_uart_byte(uint8_t byte)
{
    if (high_score_prompt.active)
    {
        high_score_prompt_process_char((char)byte);
        return;
    }

    if (seed_state.collecting)
    {
        if (byte >= '0' && byte <= '9')
        {
            seed_state.value = (seed_state.value << 4) | (uint32_t)(byte - '0');
            ++seed_state.count;
        }
        else if (byte >= 'a' && byte <= 'f')
        {
            seed_state.value = (seed_state.value << 4) | (uint32_t)(10 + (byte - 'a'));
            ++seed_state.count;
        }
        else
        {
            seed_state.invalid = true;
            seed_state.collecting = false;
            return;
        }

        if (seed_state.count >= 8U)
        {
            if (!seed_state.invalid)
            {
                pending_seed = seed_state.value;
                has_pending_seed = true;
            }
            seed_state.collecting = false;
        }
        return;
    }

    switch (byte)
    {
    case '1':
    case 'q':
        enqueue_input(0U, INPUT_SOURCE_UART);
        break;
    case '2':
    case 'w':
        enqueue_input(1U, INPUT_SOURCE_UART);
        break;
    case '3':
    case 'e':
        enqueue_input(2U, INPUT_SOURCE_UART);
        break;
    case '4':
    case 'r':
        enqueue_input(3U, INPUT_SOURCE_UART);
        break;
    case ',':
    case 'k':
        update_frequencies(1);
        break;
    case '.':
    case 'l':
        update_frequencies(-1);
        break;
    case '0':
    case 'p':
        reset_requested = true;
        seed_state.collecting = false;
        break;
    case '9':
    case 'o':
        seed_state.collecting = true;
        seed_state.count = 0;
        seed_state.value = 0;
        seed_state.invalid = false;
        break;
    default:
        break;
    }
}

static bool high_scores_qualifies(uint16_t score)
{
    if (high_score_count < HIGH_SCORE_LIMIT)
        return true;
    uint16_t lowest = high_scores[high_score_count - 1U].score;
    return score >= lowest;
}

static void high_score_prompt_begin(uint16_t score)
{
    seed_state.collecting = false;
    seed_state.invalid = false;
    seed_state.count = 0;
    high_score_prompt.active = true;
    high_score_prompt.score = score;
    high_score_prompt.length = 0;
    high_score_prompt.buffer[0] = '\0';
    high_score_prompt.prompt_time = timer_millis();
    high_score_prompt.last_input_time = high_score_prompt.prompt_time;
    uart_send_string("Enter name: ");
}

static void high_score_prompt_process_char(char c)
{
    high_score_prompt.last_input_time = timer_millis();
    if (c == '\n')
    {
        high_score_prompt.active = false;
        high_score_prompt.buffer[high_score_prompt.length] = '\0';
        high_scores_add(high_score_prompt.buffer, high_score_prompt.score);
        uart_send_char('\n');
        high_scores_transmit();
        return;
    }
    if (c == '\r')
    {
        return;
    }
    if (high_score_prompt.length < HIGH_SCORE_NAME_LENGTH)
    {
        high_score_prompt.buffer[high_score_prompt.length++] = c;
        high_score_prompt.buffer[high_score_prompt.length] = '\0';
    }
}

static void high_score_prompt_update(void)
{
    if (!high_score_prompt.active)
        return;
    uint32_t now = timer_millis();
    if (high_score_prompt.length == 0)
    {
        if ((uint32_t)(now - high_score_prompt.prompt_time) >= HIGH_SCORE_TIMEOUT_MS)
        {
            high_score_prompt.active = false;
            high_score_prompt.buffer[0] = '\0';
            high_scores_add(high_score_prompt.buffer, high_score_prompt.score);
            uart_send_char('\n');
            high_scores_transmit();
        }
    }
    else if ((uint32_t)(now - high_score_prompt.last_input_time) >= HIGH_SCORE_TIMEOUT_MS)
    {
        high_score_prompt.active = false;
        high_score_prompt.buffer[high_score_prompt.length] = '\0';
        high_scores_add(high_score_prompt.buffer, high_score_prompt.score);
        uart_send_char('\n');
        high_scores_transmit();
    }
}

static void high_scores_add(const char *name, uint16_t score)
{
    high_score_entry_t entry;
    entry.valid = true;
    entry.score = score;
    strncpy(entry.name, name, HIGH_SCORE_NAME_LENGTH);
    entry.name[HIGH_SCORE_NAME_LENGTH] = '\0';

    if (high_score_count < HIGH_SCORE_LIMIT)
    {
        high_scores[high_score_count++] = entry;
    }
    else
    {
        high_scores[high_score_count - 1U] = entry;
    }

    for (int8_t i = (int8_t)high_score_count - 1; i > 0; --i)
    {
        if (high_scores[i].score > high_scores[i - 1].score)
        {
            high_score_entry_t tmp = high_scores[i];
            high_scores[i] = high_scores[i - 1];
            high_scores[i - 1] = tmp;
        }
    }
}

static void high_scores_transmit(void)
{
    for (uint8_t i = 0; i < high_score_count; ++i)
    {
        const high_score_entry_t *entry = &high_scores[i];
        uart_send_string(entry->name);
        uart_send_char(' ');

        char buffer[8];
        uint16_t temp = entry->score;
        uint8_t index = 0;
        if (temp == 0)
        {
            buffer[index++] = '0';
        }
        else
        {
            char digits[6];
            uint8_t count = 0;
            while (temp > 0 && count < sizeof digits)
            {
                digits[count++] = (char)('0' + (temp % 10U));
                temp /= 10U;
            }
            while (count > 0)
            {
                buffer[index++] = digits[--count];
            }
        }
        buffer[index] = '\0';
        uart_send_string(buffer);
        uart_send_char('\n');
    }
}

static void delay_with_abort(uint16_t ms)
{
    uint32_t start = timer_millis();
    while ((uint32_t)(timer_millis() - start) < ms)
    {
        if (reset_requested)
            break;
        timer_service();
    }
}
