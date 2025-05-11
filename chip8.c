#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

// SDL Container
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

// Emulator Config
typedef struct {
    uint32_t window_width;
    uint32_t window_height;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t scaling_factor;
    bool outline;
    uint32_t IPS; // instructions per second
    // bool legacy_fx55_fx65_I_behavior; // Optional: for configurable I behavior
} config_t;

// Instruction Structure
typedef struct {
    uint16_t opcode;
    uint16_t NNN; // 12 bit address constant
    uint8_t NN;   // 8 bit constant
    uint8_t N;    // 4 bit constant (nibble)
    uint8_t X;    // 4 bit register identifier
    uint8_t Y;    // 4 bit register identifier
} instruction_t;

// Emulator States
typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

// Chip-8 Machine State
typedef struct {
    emulator_state_t state;
    uint8_t V[16];          // General purpose registers V0-VF
    uint16_t I;             // Index register
    uint16_t PC;            // Program Counter
    uint16_t stack[12];     // Stack (CHIP-8 typically has 16 levels, 12 is a common modern choice)
    uint16_t *stack_pointer; // Stack Pointer
    bool keypad[16];        // Keypad state (16 keys)
    uint8_t ram[4096];      // Memory (4KB)
    bool display[64*32];    // Display buffer (64x32 pixels)
    uint8_t delay_timer;    // Decrements at 60Hz if > 0
    uint8_t sound_timer;    // Decrements at 60Hz if > 0, buzzes when it reaches 0
    instruction_t instruction; // Current decoded instruction
    const char *rom_name;   // Name of the loaded ROM
} chip8_t;


// Initialize SDL
bool init_sdl(sdl_t *sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL initialization failed. %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow(
        "CHIP-8 Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config.window_width * config.scaling_factor,
        config.window_height * config.scaling_factor,
        0); // Window flags

    if (!sdl->window) {
        SDL_Log("SDL window creation failed: %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer) {
        SDL_Log("Renderer creation failed: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

// Set emulator configuration from arguments (or defaults)
bool set_config_from_args(config_t *config, int argc, char **argv) {
    // Default configuration
    *config = (config_t) {
        .window_width = 64,         // CHIP-8 native width
        .window_height = 32,        // CHIP-8 native height
        .fg_color = 0xFFFFFFFF,     // Foreground
        .bg_color = 0x000000FF,     // Background
        .scaling_factor = 20,       // Pixel scaling factor for window size
        .outline = false,           // Whether to draw outlines around pixels
        .IPS = 700,                 // Instructions Per Second (approximate target)
        // .legacy_fx55_fx65_I_behavior = false // Default to modern behavior
    };

    // TODO: Implement actual command-line argument parsing here if needed
    // Example: to change scaling_factor, IPS, colors, etc.
    // For now, it just uses defaults and suppresses unused parameter warnings.
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning

    return true;
}

// Initialize Chip-8 machine state
bool init_chip8(chip8_t *chip8, const char rom_name[]) {
    const uint32_t entry_point = 0x200; // CHIP-8 programs start at memory location 0x200
    memset(chip8, 0, sizeof(chip8_t)); // Clear all chip8 struct fields to zero/false

    // Load Fontset (0-F) into RAM (addresses 0x000-0x04F)
    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };
    memcpy(&chip8->ram[0x000], font, sizeof(font)); // Load font starting at address 0x000

    // Load ROM into RAM
    FILE *rom_file = fopen(rom_name, "rb"); // Open ROM file in binary read mode
    if (!rom_file) {
        SDL_Log("Failed to open ROM: %s\n", rom_name);
        return false;
    }

    // Get ROM size
    fseek(rom_file, 0, SEEK_END);
    long rom_size = ftell(rom_file);
    const long max_rom_size = sizeof(chip8->ram) - entry_point;
    rewind(rom_file); // Go back to the beginning of the file

    if (rom_size > max_rom_size) {
        SDL_Log("ROM file %s is too large. Size: %ld bytes, Max allowed: %ld bytes\n",
                  rom_name, rom_size, max_rom_size);
        fclose(rom_file);
        return false;
    }
     if (rom_size <= 0) { // Check for empty or invalid file size
        SDL_Log("ROM file %s is empty or has invalid size: %ld bytes\n", rom_name, rom_size);
        fclose(rom_file);
        return false;
    }


    // Read ROM into RAM starting at the entry point
    if (fread(&chip8->ram[entry_point], 1, rom_size, rom_file) != (size_t)rom_size) { // Check bytes read
        SDL_Log("Failed to read ROM %s into RAM (read %ld bytes).\n", rom_name, ftell(rom_file));
        fclose(rom_file);
        return false;
    }
    fclose(rom_file);

    // Set initial emulator state
    chip8->state = RUNNING;
    chip8->PC = entry_point;        // Start execution at 0x200
    chip8->rom_name = rom_name;
    chip8->stack_pointer = &chip8->stack[0]; // SP points to the beginning of the stack array

    // Seed random number generator once
    srand(time(NULL));

    return true;
}

// Cleanup SDL resources
void cleanup(const sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

// Clear the SDL screen to the background color
void clear_screen_sdl(const sdl_t sdl, config_t config) {
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >>  8) & 0xFF;
    const uint8_t a = (config.bg_color >>  0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

// Update the SDL screen with the CHIP-8 display buffer
void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8) {
    SDL_Rect pixel_rect = {.x = 0, .y = 0, .w = config.scaling_factor, .h = config.scaling_factor};

    // Foreground color components
    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const uint8_t fg_b = (config.fg_color >>  8) & 0xFF;
    const uint8_t fg_a = (config.fg_color >>  0) & 0xFF;

    // Background color components (used for outline if enabled, or drawing "off" pixels)
    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >>  8) & 0xFF;
    const uint8_t bg_a = (config.bg_color >>  0) & 0xFF;

    const uint32_t total_pixels = config.window_width * config.window_height;

    for (uint32_t i = 0; i < total_pixels; i++) {
        pixel_rect.x = (i % config.window_width) * config.scaling_factor;
        pixel_rect.y = (i / config.window_width) * config.scaling_factor;

        if (chip8.display[i]) { // If the CHIP-8 pixel is "on"
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &pixel_rect);

            if (config.outline) { // Optionally draw an outline for "on" pixels
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a); // Outline with background color
                SDL_RenderDrawRect(sdl.renderer, &pixel_rect);
            }
        } else { // If the CHIP-8 pixel is "off"
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &pixel_rect);
        }
    }
    SDL_RenderPresent(sdl.renderer); // Show the drawn frame
}

/*
CHIP-8 Keypad Layout   |   QWERTY Keyboard Mapping
---------------------|--------------------------
1  2  3  C            |   1  2  3  4
4  5  6  D            |   Q  W  E  R
7  8  9  E            |   A  S  D  F
A  0  B  F            |   Z  X  C  V
*/
// Handle user input
void get_input(chip8_t *chip8) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                return; // Exit input handling immediately on quit

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: chip8->state = QUIT; return;
                    case SDLK_SPACE: // Toggle pause/run
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;
                            puts("Emulator Paused.");
                        } else if (chip8->state == PAUSED) {
                            chip8->state = RUNNING;
                            puts("Emulator Resumed.");
                        }
                        break;

                    // CHIP-8 Key to QWERTY Mapping
                    case SDLK_1: chip8->keypad[0x1] = true; break;
                    case SDLK_2: chip8->keypad[0x2] = true; break;
                    case SDLK_3: chip8->keypad[0x3] = true; break;
                    case SDLK_4: chip8->keypad[0xC] = true; break; // C

                    case SDLK_q: chip8->keypad[0x4] = true; break;
                    case SDLK_w: chip8->keypad[0x5] = true; break;
                    case SDLK_e: chip8->keypad[0x6] = true; break;
                    case SDLK_r: chip8->keypad[0xD] = true; break; // D

                    case SDLK_a: chip8->keypad[0x7] = true; break;
                    case SDLK_s: chip8->keypad[0x8] = true; break;
                    case SDLK_d: chip8->keypad[0x9] = true; break;
                    case SDLK_f: chip8->keypad[0xE] = true; break; // E

                    case SDLK_z: chip8->keypad[0xA] = true; break; // A (Mapped to Z)
                    case SDLK_x: chip8->keypad[0x0] = true; break; // 0 (Mapped to X)
                    case SDLK_c: chip8->keypad[0xB] = true; break; // B (Mapped to C)
                    case SDLK_v: chip8->keypad[0xF] = true; break; // F (Mapped to V)
                    default: break; // Ignore other keys
                }
                break;

            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
                    // CHIP-8 Key to QWERTY Mapping
                    case SDLK_1: chip8->keypad[0x1] = false; break;
                    case SDLK_2: chip8->keypad[0x2] = false; break;
                    case SDLK_3: chip8->keypad[0x3] = false; break;
                    case SDLK_4: chip8->keypad[0xC] = false; break;

                    case SDLK_q: chip8->keypad[0x4] = false; break;
                    case SDLK_w: chip8->keypad[0x5] = false; break;
                    case SDLK_e: chip8->keypad[0x6] = false; break;
                    case SDLK_r: chip8->keypad[0xD] = false; break;

                    case SDLK_a: chip8->keypad[0x7] = false; break;
                    case SDLK_s: chip8->keypad[0x8] = false; break;
                    case SDLK_d: chip8->keypad[0x9] = false; break;
                    case SDLK_f: chip8->keypad[0xE] = false; break;

                    case SDLK_z: chip8->keypad[0xA] = false; break; // A
                    case SDLK_x: chip8->keypad[0x0] = false; break; // 0
                    case SDLK_c: chip8->keypad[0xB] = false; break; // B
                    case SDLK_v: chip8->keypad[0xF] = false; break; // F
                    default: break; // Ignore other keys
                }
                break;
            default: break; // Ignore other event types
        }
    }
}

// Print debug information about the current instruction (if DEBUG is defined)
#ifdef DEBUG
    void print_debug_info(chip8_t *chip8, config_t config) {
        // PC points to the *next* instruction, so subtract 2 for the current one
        printf("Address: 0x%04X, Opcode: 0x%04X, Desc: ",
                chip8->PC-2, chip8->instruction.opcode);

        switch((chip8->instruction.opcode >> 12) & 0x0F) { // High nibble of opcode
            case 0x0:
                if (chip8->instruction.NN == 0xE0) { // 00E0: CLS
                    printf("Clear Screen\n");
                } else if (chip8->instruction.NN == 0xEE) { // 00EE: RET
                    // Show address it would return to (top of stack)
                    printf("Return from subroutine to address 0x%04X\n",
                            (chip8->stack_pointer > chip8->stack) ? *(chip8->stack_pointer - 1) : 0xFFFF);
                } else { // 0NNN: SYS addr (typically ignored by modern interpreters)
                    printf("SYS call to 0x%03X (No-op or Unimplemented)\n", chip8->instruction.NNN);
                }
                break;

            case 0x1: // 1NNN: JP addr
                printf("Jump to address 0x%03X\n", chip8->instruction.NNN);
                break;

            case 0x2: // 2NNN: CALL addr
                // PC shown here is the one *after* fetching this instruction, which is what's pushed.
                printf("Call subroutine at 0x%03X (PC pushed: 0x%04X)\n", chip8->instruction.NNN, chip8->PC);
                break;

            case 0x3: // 3XNN: SE Vx, byte
                printf("Skip next if V%X (0x%02X) == 0x%02X\n",
                    chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.NN);
                break;

            case 0x4: // 4XNN: SNE Vx, byte
                printf("Skip next if V%X (0x%02X) != 0x%02X\n",
                    chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.NN);
                break;

            case 0x5: // 5XY0: SE Vx, Vy
                if (chip8->instruction.N == 0) { // Check last nibble is 0
                    printf("Skip next if V%X (0x%02X) == V%X (0x%02X)\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X],
                        chip8->instruction.Y, chip8->V[chip8->instruction.Y]);
                } else {
                     printf("Invalid 0x5XYN opcode (N != 0): 0x%04X\n", chip8->instruction.opcode);
                }
                break;

            case 0x6: // 6XNN: LD Vx, byte
                printf("Set V%X = 0x%02X\n", chip8->instruction.X, chip8->instruction.NN);
                break;

            case 0x7: // 7XNN: ADD Vx, byte
                // Show what the result of V[X] + NN would be (before the actual operation in emulate_instr)
                printf("Set V%X (0x%02X) += 0x%02X. Result: 0x%02X\n",
                    chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.NN,
                    (uint8_t)(chip8->V[chip8->instruction.X] + chip8->instruction.NN));
                break;

            case 0x8: // 8XYN: Math/Logic operations
                switch (chip8->instruction.N) { // Check last nibble
                    case 0: // 8XY0: LD Vx, Vy
                        printf("Set V%X = V%X (0x%02X)\n",
                        chip8->instruction.X, chip8->instruction.Y, chip8->V[chip8->instruction.Y]);
                        break;
                    case 1: // 8XY1: OR Vx, Vy
                        printf("Set V%X (0x%02X) |= V%X (0x%02X). Result: 0x%02X\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X],
                        chip8->instruction.Y, chip8->V[chip8->instruction.Y],
                        chip8->V[chip8->instruction.X] | chip8->V[chip8->instruction.Y]);
                        break;
                    case 2: // 8XY2: AND Vx, Vy
                        printf("Set V%X (0x%02X) &= V%X (0x%02X). Result: 0x%02X\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X],
                        chip8->instruction.Y, chip8->V[chip8->instruction.Y],
                        chip8->V[chip8->instruction.X] & chip8->V[chip8->instruction.Y]);
                        break;
                    case 3: // 8XY3: XOR Vx, Vy
                        printf("Set V%X (0x%02X) ^= V%X (0x%02X). Result: 0x%02X\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X],
                        chip8->instruction.Y, chip8->V[chip8->instruction.Y],
                        chip8->V[chip8->instruction.X] ^ chip8->V[chip8->instruction.Y]);
                        break;
                    case 4: // 8XY4: ADD Vx, Vy
                        printf("Set V%X (0x%02X) += V%X (0x%02X). Result: 0x%02X, VF = %X (1 if carry)\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X],
                        chip8->instruction.Y, chip8->V[chip8->instruction.Y],
                        (uint8_t)(chip8->V[chip8->instruction.X] + chip8->V[chip8->instruction.Y]),
                        ((uint16_t)chip8->V[chip8->instruction.X] + chip8->V[chip8->instruction.Y] > 0xFF));
                        break;
                    case 5: // 8XY5: SUB Vx, Vy
                        printf("Set V%X (0x%02X) -= V%X (0x%02X). Result: 0x%02X, VF = %X (1 if NO borrow)\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X],
                        chip8->instruction.Y, chip8->V[chip8->instruction.Y],
                        (uint8_t)(chip8->V[chip8->instruction.X] - chip8->V[chip8->instruction.Y]),
                        (chip8->V[chip8->instruction.X] >= chip8->V[chip8->instruction.Y]));
                        break;
                    case 6: // 8XY6: SHR Vx {, Vy}
                        // Assuming original CHIP-8 behavior: Vx is shifted. VF gets LSB.
                        printf("Set V%X (0x%02X) >>= 1. Result: 0x%02X, VF = %X (LSB before shift)\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X], // Value of Vx used in the operation
                        chip8->V[chip8->instruction.X] >> 1,
                        chip8->V[chip8->instruction.X] & 1);
                        break;
                    case 7: // 8XY7: SUBN Vx, Vy
                        printf("Set V%X = V%X (0x%02X) - V%X (0x%02X). Result: 0x%02X, VF = %X (1 if NO borrow)\n",
                        chip8->instruction.X, chip8->instruction.Y, chip8->V[chip8->instruction.Y],
                        chip8->instruction.X, chip8->V[chip8->instruction.X],
                        (uint8_t)(chip8->V[chip8->instruction.Y] - chip8->V[chip8->instruction.X]),
                        (chip8->V[chip8->instruction.Y] >= chip8->V[chip8->instruction.X]));
                        break;
                    case 0xE: // 8XYE: SHL Vx {, Vy}
                        // Assuming original CHIP-8 behavior: Vx is shifted. VF gets MSB.
                        printf("Set V%X (0x%02X) <<= 1. Result: 0x%02X, VF = %X (MSB before shift)\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X], // Value of Vx used
                        (uint8_t)(chip8->V[chip8->instruction.X] << 1),
                        (chip8->V[chip8->instruction.X] & 0x80) >> 7);
                        break;
                    default:
                        printf("Invalid 0x8XYN opcode (N unknown): 0x%04X\n", chip8->instruction.opcode);
                        break;
                }
                break;

            case 0x9: // 9XY0: SNE Vx, Vy
                 if (chip8->instruction.N == 0) { // Check last nibble is 0
                    printf("Skip next if V%X (0x%02X) != V%X (0x%02X)\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X],
                        chip8->instruction.Y, chip8->V[chip8->instruction.Y]);
                } else {
                     printf("Invalid 0x9XYN opcode (N != 0): 0x%04X\n", chip8->instruction.opcode);
                }
                break;

            case 0xA: // ANNN: LD I, addr
                printf("Set I = 0x%03X\n", chip8->instruction.NNN);
                break;

            case 0xB: // BNNN: JP V0, addr
                printf("Jump to V0 (0x%02X) + 0x%03X. Result PC = 0x%04X\n",
                    chip8->V[0], chip8->instruction.NNN, (uint16_t)(chip8->V[0] + chip8->instruction.NNN));
                break;

            case 0xC: // CXNN: RND Vx, byte
                printf("Set V%X = rand() %% 256 & 0x%02X\n", // rand() result will be ANDed with NN
                    chip8->instruction.X, chip8->instruction.NN);
                break;

            case 0xD: // DXYN: DRW Vx, Vy, nibble
                printf("Draw N=%u height sprite at V%X (0x%02X), V%X (0x%02X) from I (0x%04X). VF = collision.\n",
                    chip8->instruction.N, chip8->instruction.X, chip8->V[chip8->instruction.X],
                    chip8->instruction.Y, chip8->V[chip8->instruction.Y], chip8->I);
                break;

            case 0xE: // EXNN Key operations
                if (chip8->instruction.NN == 0x9E) { // EX9E: SKP Vx
                    printf("Skip next if key V%X (key_code=0x%X) is pressed. Keypad val: %d\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X] & 0xF, // Mask to ensure 0-F
                        chip8->keypad[chip8->V[chip8->instruction.X] & 0xF]);
                } else if (chip8->instruction.NN == 0xA1) { // EXA1: SKNP Vx
                    printf("Skip next if key V%X (key_code=0x%X) is NOT pressed. Keypad val: %d\n",
                        chip8->instruction.X, chip8->V[chip8->instruction.X] & 0xF, // Mask to ensure 0-F
                        chip8->keypad[chip8->V[chip8->instruction.X] & 0xF]);
                } else {
                    printf("Invalid 0xEXNN opcode: 0x%04X\n", chip8->instruction.opcode);
                }
                break;

            case 0xF: // FXNN Miscellaneous operations
                switch (chip8->instruction.NN) {
                    case 0x07: // FX07: LD Vx, DT
                        printf("Set V%X = Delay Timer (0x%02X)\n",
                            chip8->instruction.X, chip8->delay_timer);
                        break;
                    case 0x0A: // FX0A: LD Vx, K (Wait for key press)
                        printf("Wait for key press, store in V%X\n", chip8->instruction.X);
                        break;
                    case 0x15: // FX15: LD DT, Vx
                        printf("Set Delay Timer = V%X (0x%02X)\n",
                            chip8->instruction.X, chip8->V[chip8->instruction.X]);
                        break;
                    case 0x18: // FX18: LD ST, Vx
                        printf("Set Sound Timer = V%X (0x%02X)\n",
                            chip8->instruction.X, chip8->V[chip8->instruction.X]);
                        break;
                    case 0x1E: // FX1E: ADD I, Vx
                        printf("Set I (0x%04X) += V%X (0x%02X). Result I: 0x%04X\n",
                            chip8->I, chip8->instruction.X, chip8->V[chip8->instruction.X],
                            (uint16_t)(chip8->I + chip8->V[chip8->instruction.X]));
                        break;
                    case 0x29: // FX29: LD F, Vx (Load font sprite)
                        printf("Set I = sprite location for char in V%X (0x%02X -> char '%X'). Result I: 0x%04X\n",
                            chip8->instruction.X, chip8->V[chip8->instruction.X],
                            chip8->V[chip8->instruction.X] & 0xF, // Mask to ensure 0-F
                            (uint16_t)((chip8->V[chip8->instruction.X] & 0xF) * 5)); // Font sprites are 5 bytes each
                        break;
                    case 0x33: // FX33: LD B, Vx (Store BCD)
                        printf("Store BCD of V%X (0x%02X) at I(0x%04X), I+1, I+2\n",
                            chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->I);
                        break;
                    case 0x55: // FX55: LD [I], Vx (Register dump)
                        printf("Register dump V0-V%X into memory from I (0x%04X). I becomes I+X+1 for some.\n",
                            chip8->instruction.X, chip8->I);
                        break;
                    case 0x65: // FX65: LD Vx, [I] (Register load)
                        printf("Register load V0-V%X from memory from I (0x%04X). I becomes I+X+1 for some.\n",
                            chip8->instruction.X, chip8->I);
                        break;
                    default:
                        printf("Invalid 0xFXNN opcode: 0x%04X\n", chip8->instruction.opcode);
                        break;
                }
                break;

            default: // Default for the main opcode switch (unknown high nibble)
                printf("Unimplemented or Invalid high nibble for opcode: 0x%04X\n", chip8->instruction.opcode);
                break;
        }
        (void)config; // Suppress unused parameter warning if config is not used in debug
    }
#endif

// Emulate one CHIP-8 instruction
void emulate_instr(chip8_t *chip8, const config_t config) {
    // Fetch Opcode (2 bytes, big-endian)
    chip8->instruction.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC + 1];
    chip8->PC += 2; // Increment Program Counter for next instruction

    // Decode Opcode into components
    chip8->instruction.NNN = chip8->instruction.opcode & 0x0FFF; // Lowest 12 bits (address)
    chip8->instruction.NN  = chip8->instruction.opcode & 0x00FF; // Lowest 8 bits (byte value)
    chip8->instruction.N   = chip8->instruction.opcode & 0x000F; // Lowest 4 bits (nibble value)
    chip8->instruction.X   = (chip8->instruction.opcode >> 8) & 0x0F; // Lower 4 bits of the high byte (Vx register index)
    chip8->instruction.Y   = (chip8->instruction.opcode >> 4) & 0x0F; // Upper 4 bits of the low byte (Vy register index)

    #ifdef DEBUG
        print_debug_info(chip8, config);
    #endif

    // Execute Opcode based on the highest 4 bits
    switch((chip8->instruction.opcode >> 12) & 0x0F) {
        case 0x00: // Miscellaneous instructions (0NNN, 00E0, 00EE)
            switch(chip8->instruction.NN) { // Check the lowest byte
                case 0xE0: // 00E0: CLS - Clear the display
                    memset(chip8->display, false, sizeof(chip8->display));
                    break;
                case 0xEE: // 00EE: RET - Return from a subroutine
                    if (chip8->stack_pointer > chip8->stack) { // Check for stack underflow
                        chip8->stack_pointer--;                 // Decrement Stack Pointer
                        chip8->PC = *chip8->stack_pointer;      // Set PC to address from stack
                    } else {
                        #ifdef DEBUG
                        fprintf(stderr, "Error: Stack underflow on RET (PC: 0x%04X)!\n", chip8->PC - 2);
                        #endif
                        chip8->state = PAUSED; // Pause on error
                    }
                    break;
                default: // 0NNN: SYS addr - Jump to a machine code routine (Ignored on modern interpreters)
                    // Typically a No-Op for emulators.
                    break;
            }
            break;

        case 0x01: // 1NNN: JP addr - Jump to location NNN
            chip8->PC = chip8->instruction.NNN;
            break;

        case 0x02: // 2NNN: CALL addr - Call subroutine at NNN
            // Check for stack overflow before pushing
            if (chip8->stack_pointer < &chip8->stack[sizeof(chip8->stack)/sizeof(chip8->stack[0])]) {
                *chip8->stack_pointer = chip8->PC; // Store current PC (which is already advanced) on stack
                chip8->stack_pointer++;            // Increment Stack Pointer
                chip8->PC = chip8->instruction.NNN; // Set PC to subroutine address NNN
            } else {
                 #ifdef DEBUG
                fprintf(stderr, "Error: Stack overflow on CALL (PC: 0x%04X, Target: 0x%03X)!\n", chip8->PC - 2, chip8->instruction.NNN);
                #endif
                chip8->state = PAUSED; // Pause on error
            }
            break;

        case 0x03: // 3XNN: SE Vx, byte - Skip next instruction if V[X] == NN
            if (chip8->V[chip8->instruction.X] == chip8->instruction.NN) {
                chip8->PC += 2; // Skip one 2-byte instruction
            }
            break;

        case 0x04: // 4XNN: SNE Vx, byte - Skip next instruction if V[X] != NN
            if (chip8->V[chip8->instruction.X] != chip8->instruction.NN) {
                chip8->PC += 2;
            }
            break;

        case 0x05: // 5XY0: SE Vx, Vy - Skip next instruction if V[X] == V[Y]
            if (chip8->instruction.N == 0) { // Ensure last nibble is 0 for valid opcode
                if (chip8->V[chip8->instruction.X] == chip8->V[chip8->instruction.Y]) {
                    chip8->PC += 2;
                }
            } else { // Invalid opcode (5XYN where N!=0)
                 #ifdef DEBUG
                fprintf(stderr, "Warning: Invalid 5XYN opcode 0x%04X (N!=0) (PC: 0x%04X)\n", chip8->instruction.opcode, chip8->PC-2);
                #endif
            }
            break;

        case 0x06: // 6XNN: LD Vx, byte - Set V[X] = NN
            chip8->V[chip8->instruction.X] = chip8->instruction.NN;
            break;

        case 0x07: // 7XNN: ADD Vx, byte - Set V[X] = V[X] + NN (VF is not affected)
            chip8->V[chip8->instruction.X] += chip8->instruction.NN;
            break;

        case 0x08: // 8XYN: Logical and arithmetic operations
            switch (chip8->instruction.N) { // Check the last nibble (N)
                case 0: // 8XY0: LD Vx, Vy - Set V[X] = V[Y]
                    chip8->V[chip8->instruction.X] = chip8->V[chip8->instruction.Y];
                    break;
                case 1: // 8XY1: OR Vx, Vy - Set V[X] = V[X] OR V[Y]
                    chip8->V[chip8->instruction.X] |= chip8->V[chip8->instruction.Y];
                    // Optional: chip8->V[0xF] = 0; // Some interpreters clear VF here
                    break;
                case 2: // 8XY2: AND Vx, Vy - Set V[X] = V[X] AND V[Y]
                    chip8->V[chip8->instruction.X] &= chip8->V[chip8->instruction.Y];
                    // Optional: chip8->V[0xF] = 0;
                    break;
                case 3: // 8XY3: XOR Vx, Vy - Set V[X] = V[X] XOR V[Y]
                    chip8->V[chip8->instruction.X] ^= chip8->V[chip8->instruction.Y];
                    // Optional: chip8->V[0xF] = 0;
                    break;
                case 4: { // 8XY4: ADD Vx, Vy - Set V[X] = V[X] + V[Y], V[F] = carry.
                    uint16_t sum = (uint16_t)chip8->V[chip8->instruction.X] + chip8->V[chip8->instruction.Y];
                    chip8->V[0xF] = (sum > 0xFF) ? 1 : 0; // Set carry flag if overflow
                    chip8->V[chip8->instruction.X] = (uint8_t)sum; // Store lowest 8 bits of sum in V[X]
                    break;
                }
                case 5: // 8XY5: SUB Vx, Vy - Set V[X] = V[X] - V[Y], V[F] = NOT borrow.
                    chip8->V[0xF] = (chip8->V[chip8->instruction.X] >= chip8->V[chip8->instruction.Y]) ? 1 : 0;
                    chip8->V[chip8->instruction.X] -= chip8->V[chip8->instruction.Y];
                    break;
                case 6: // 8XY6: SHR Vx {, Vy} - Set V[X] = V[X] SHR 1.
                    // Original CHIP-8: Vx = Vx >> 1. VF = LSB of Vx BEFORE shift.
                    // Some SCHIP variants/emulators: Vx = Vy >> 1. This emulates original.
                    // if (config.legacy_8XY6_shift_vy) { // Optional config
                    //    chip8->V[0xF] = chip8->V[chip8->instruction.Y] & 0x1;
                    //    chip8->V[chip8->instruction.X] = chip8->V[chip8->instruction.Y] >> 1;
                    // } else {
                        chip8->V[0xF] = chip8->V[chip8->instruction.X] & 0x1; // LSB to VF
                        chip8->V[chip8->instruction.X] >>= 1;
                    // }
                    break;
                case 7: // 8XY7: SUBN Vx, Vy - Set V[X] = V[Y] - V[X], V[F] = NOT borrow.
                    chip8->V[0xF] = (chip8->V[chip8->instruction.Y] >= chip8->V[chip8->instruction.X]) ? 1 : 0;
                    chip8->V[chip8->instruction.X] = chip8->V[chip8->instruction.Y] - chip8->V[chip8->instruction.X];
                    break;
                case 0xE: // 8XYE: SHL Vx {, Vy} - Set V[X] = V[X] SHL 1.
                    // Original CHIP-8: Vx = Vx << 1. VF = MSB of Vx BEFORE shift.
                    // Some SCHIP variants/emulators: Vx = Vy << 1. This emulates original.
                    // if (config.legacy_8XYE_shift_vy) { // Optional config
                    //    chip8->V[0xF] = (chip8->V[chip8->instruction.Y] & 0x80) >> 7;
                    //    chip8->V[chip8->instruction.X] = chip8->V[chip8->instruction.Y] << 1;
                    // } else {
                        chip8->V[0xF] = (chip8->V[chip8->instruction.X] & 0x80) >> 7; // MSB to VF
                        chip8->V[chip8->instruction.X] <<= 1;
                    // }
                    break;
                default:
                    #ifdef DEBUG
                    fprintf(stderr, "Warning: Unhandled 8XYN opcode 0x%04X (PC: 0x%04X)\n", chip8->instruction.opcode, chip8->PC-2);
                    #endif
                    break;
            }
            break;

        case 0x09: // 9XY0: SNE Vx, Vy - Skip next instruction if V[X] != V[Y]
            if (chip8->instruction.N == 0) { // Ensure last nibble is 0
                if (chip8->V[chip8->instruction.X] != chip8->V[chip8->instruction.Y]) {
                    chip8->PC += 2;
                }
            } else { // Invalid opcode (9XYN where N!=0)
                 #ifdef DEBUG
                fprintf(stderr, "Warning: Invalid 9XYN opcode 0x%04X (N!=0) (PC: 0x%04X)\n", chip8->instruction.opcode, chip8->PC-2);
                #endif
            }
            break;

        case 0x0A: // ANNN: LD I, addr - Set I = NNN
            chip8->I = chip8->instruction.NNN;
            break;

        case 0x0B: // BNNN: JP V0, addr - Jump to location NNN + V[0]
            chip8->PC = chip8->instruction.NNN + chip8->V[0];
            break;

        case 0x0C: // CXNN: RND Vx, byte - Set V[X] = random byte AND NN
            chip8->V[chip8->instruction.X] = (rand() % 256) & chip8->instruction.NN;
            break;

        case 0x0D: { // DXYN: DRW Vx, Vy, nibble - Display N-byte sprite starting at memory I at (VX, VY), set VF = collision.
            uint8_t x_coord = chip8->V[chip8->instruction.X];
            uint8_t y_coord = chip8->V[chip8->instruction.Y];
            uint8_t height = chip8->instruction.N;
            uint8_t sprite_byte;
            uint32_t screen_idx;

            chip8->V[0xF] = 0; // Reset collision flag

            for (uint8_t row = 0; row < height; row++) {
                // Ensure sprite data is read within RAM bounds
                if ((size_t)chip8->I + row >= sizeof(chip8->ram)) {
                    #ifdef DEBUG
                    fprintf(stderr, "Warning: Sprite draw (DXYN) attempting to read I (0x%04X + %u) out of RAM bounds (PC: 0x%04X).\n", chip8->I, row, chip8->PC-2);
                    #endif
                    break; // Stop if trying to read sprite data out of RAM
                }
                sprite_byte = chip8->ram[chip8->I + row];

                for (uint8_t col_bit = 0; col_bit < 8; col_bit++) {
                    // Calculate actual screen coordinates with wrapping
                    uint8_t current_x = (x_coord + col_bit) % config.window_width;
                    uint8_t current_y = (y_coord + row) % config.window_height;

                    if ((sprite_byte & (0x80 >> col_bit))) { // If current sprite pixel bit is 1
                        screen_idx = current_y * config.window_width + current_x;
                        
                        // This check should technically be redundant if width/height are correct
                        // but as a safeguard:
                        if (screen_idx < (config.window_width * config.window_height)) {
                            if (chip8->display[screen_idx]) { // Collision
                                chip8->V[0xF] = 1;
                            }
                            chip8->display[screen_idx] ^= 1; // XOR pixel
                        }
                    }
                }
            }
            break;
        }


        case 0x0E: // EXNN: Key-related operations
            switch (chip8->instruction.NN) {
                case 0x9E: // EX9E: SKP Vx - Skip next instruction if key with the value of V[X] is pressed
                    if (chip8->keypad[chip8->V[chip8->instruction.X] & 0xF]) { // Mask VX to 0-F
                        chip8->PC += 2;
                    }
                    break;
                case 0xA1: // EXA1: SKNP Vx - Skip next instruction if key with the value of V[X] is NOT pressed
                    if (!chip8->keypad[chip8->V[chip8->instruction.X] & 0xF]) { // Mask VX to 0-F
                        chip8->PC += 2;
                    }
                    break;
                default:
                     #ifdef DEBUG
                    fprintf(stderr, "Warning: Unhandled EXNN opcode 0x%04X (PC: 0x%04X)\n", chip8->instruction.opcode, chip8->PC-2);
                    #endif
                    break;
            }
            break;

        case 0x0F: // FXNN: Miscellaneous operations
            switch (chip8->instruction.NN) {
                case 0x07: // FX07: LD Vx, DT - Set V[X] = delay timer value
                    chip8->V[chip8->instruction.X] = chip8->delay_timer;
                    break;
                case 0x0A: { // FX0A: LD Vx, K - Wait for a key press, store the value of the key in V[X]
                    bool key_pressed_this_cycle = false;
                    for (uint8_t i = 0; i < sizeof(chip8->keypad)/sizeof(chip8->keypad[0]); i++) { // Iterate 0-15
                        if (chip8->keypad[i]) {
                            chip8->V[chip8->instruction.X] = i;
                            key_pressed_this_cycle = true;
                            break;
                        }
                    }
                    if (!key_pressed_this_cycle) {
                        chip8->PC -= 2; // Repeat instruction
                    }
                    break;
                }
                case 0x15: // FX15: LD DT, Vx - Set delay timer = V[X]
                    chip8->delay_timer = chip8->V[chip8->instruction.X];
                    break;
                case 0x18: // FX18: LD ST, Vx - Set sound timer = V[X]
                    chip8->sound_timer = chip8->V[chip8->instruction.X];
                    break;
                case 0x1E: // FX1E: ADD I, Vx - Set I = I + V[X]
                    // Undocumented: Some interpreters set VF on overflow (I > 0xFFF). Most modern ones don't.
                    // uint16_t new_I = chip8->I + chip8->V[chip8->instruction.X];
                    // if (new_I < chip8->I) chip8->V[0xF] = 1; else chip8->V[0xF] = 0; // Example if VF needed for I overflow
                    chip8->I += chip8->V[chip8->instruction.X];
                    break;
                case 0x29: // FX29: LD F, Vx - Set I = location of sprite for digit V[X]
                    chip8->I = (chip8->V[chip8->instruction.X] & 0xF) * 5; // Font sprites are 5 bytes each
                    break;
                case 0x33: { // FX33: LD B, Vx - Store BCD representation of V[X] in memory locations I, I+1, and I+2
                    uint8_t val = chip8->V[chip8->instruction.X];
                    if ((size_t)chip8->I + 2 < sizeof(chip8->ram)) {
                        chip8->ram[chip8->I]     = val / 100;        // Hundreds
                        chip8->ram[chip8->I + 1] = (val / 10) % 10;  // Tens
                        chip8->ram[chip8->I + 2] = val % 10;         // Ones
                    } else {
                        #ifdef DEBUG
                        fprintf(stderr, "Warning: BCD Store (FX33) attempting to write I (0x%04X) out of RAM bounds (PC: 0x%04X).\n", chip8->I, chip8->PC-2);
                        #endif
                    }
                    break;
                }
                case 0x55: { // FX55: LD [I], Vx - Store registers V0 through V[X] in memory starting at location I
                    // Ensure no buffer overflow.
                    if ((size_t)chip8->I + chip8->instruction.X < sizeof(chip8->ram)) {
                        for (uint8_t i = 0; i <= chip8->instruction.X; i++) {
                            chip8->ram[chip8->I + i] = chip8->V[i];
                        }
                        // Apply modern behavior: I = I + X + 1
                        // if (!config.legacy_fx55_fx65_I_behavior) { // If you add a config option
                            chip8->I += (chip8->instruction.X + 1);
                        // }
                    } else {
                         #ifdef DEBUG
                        fprintf(stderr, "Warning: Register Dump (FX55) attempting to write from I (0x%04X up to +%u) out of RAM bounds (PC: 0x%04X).\n", chip8->I, chip8->instruction.X, chip8->PC-2);
                        #endif
                    }
                    break;
                }
                case 0x65: { // FX65: LD Vx, [I] - Read registers V0 through V[X] from memory starting at location I
                    // Ensure no buffer overflow.
                    if ((size_t)chip8->I + chip8->instruction.X < sizeof(chip8->ram)) {
                        for (uint8_t i = 0; i <= chip8->instruction.X; i++) {
                            chip8->V[i] = chip8->ram[chip8->I + i];
                        }
                        // Apply modern behavior: I = I + X + 1
                        // if (!config.legacy_fx55_fx65_I_behavior) { // If you add a config option
                             chip8->I += (chip8->instruction.X + 1);
                        // }
                    } else {
                         #ifdef DEBUG
                        fprintf(stderr, "Warning: Register Load (FX65) attempting to read from I (0x%04X up to +%u) out of RAM bounds (PC: 0x%04X).\n", chip8->I, chip8->instruction.X, chip8->PC-2);
                        #endif
                    }
                    break;
                }
                default:
                     #ifdef DEBUG
                    fprintf(stderr, "Warning: Unhandled FXNN opcode 0x%04X (PC: 0x%04X)\n", chip8->instruction.opcode, chip8->PC-2);
                    #endif
                    break;
            }
            break;

        default:
            #ifdef DEBUG
            fprintf(stderr, "Warning: Unhandled high nibble for opcode 0x%04X (PC: 0x%04X)\n", chip8->instruction.opcode, chip8->PC-2);
            #endif
            break;
    }
    (void)config; // Suppress unused parameter warning if config is not used by emulate_instr directly
}

// Update CHIP-8 timers (delay and sound)
void update_timers(chip8_t *chip8) {
    if (chip8->delay_timer > 0) {
        chip8->delay_timer--;
    }

    if (chip8->sound_timer > 0) {
        chip8->sound_timer--;
        if (chip8->sound_timer == 0) {
            // TODO: Implement actual beep sound when sound timer reaches zero.
            #ifdef DEBUG
            // printf("BEEP!\n"); // Can be noisy
            #endif
        }
    }
}

// Main function
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ROM_file_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv)) {
        exit(EXIT_FAILURE);
    }

    sdl_t sdl = {0};
    if (!init_sdl(&sdl, config)) {
        exit(EXIT_FAILURE);
    }

    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name)) {
        cleanup(sdl);
        exit(EXIT_FAILURE);
    }

    clear_screen_sdl(sdl, config);

    uint32_t last_timer_update_tick = SDL_GetTicks();
    const uint32_t timer_update_interval_ms = 1000 / 60; // 60Hz

    // For timing the main loop to achieve desired IPS and FPS
    uint32_t cycle_start_tick;
    const double ms_per_instruction = 1000.0 / config.IPS;


    // Main emulator loop
    while (chip8.state != QUIT) {
        cycle_start_tick = SDL_GetTicks();

        get_input(&chip8);

        if (chip8.state == PAUSED) {
            // Update screen even when paused to show "Paused" state or current screen
            update_screen(sdl, config, chip8);
            SDL_Delay(100); // Reduce CPU usage when paused
            last_timer_update_tick = SDL_GetTicks(); // Prevent timer catch-up burst
            continue;
        }
        if (chip8.state == QUIT) {
            break;
        }

        // --- Emulation Cycle ---
        // Run one instruction
        if ((size_t)chip8.PC >= sizeof(chip8.ram) || (size_t)chip8.PC + 1 >= sizeof(chip8.ram)) {
            #ifdef DEBUG
            fprintf(stderr, "Error: PC (0x%04X) out of RAM bounds!\n", chip8.PC);
            #endif
            chip8.state = PAUSED;
            continue;
        }
        emulate_instr(&chip8, config);


        // --- Timer Updates (at 60Hz) ---
        uint32_t current_ticks = SDL_GetTicks();
        if (current_ticks - last_timer_update_tick >= timer_update_interval_ms) {
            update_timers(&chip8);
            // Also update screen at roughly 60Hz, coinciding with timer updates
            update_screen(sdl, config, chip8);
            last_timer_update_tick = current_ticks; // Or last_timer_update_tick += timer_update_interval_ms for more stable 60Hz
        }


        // --- Frame Limiting / IPS control ---
        // Calculate how long this instruction cycle took
        uint32_t instruction_time_ms = SDL_GetTicks() - cycle_start_tick;

        // If the instruction executed faster than desired IPS rate, delay
        if (instruction_time_ms < ms_per_instruction) {
            SDL_Delay((uint32_t)(ms_per_instruction - instruction_time_ms));
        }
        // If overall loop is too fast and screen hasn't updated due to timer interval,
        // we might add a small delay here too, but the screen update is tied to timer now.
        // A more robust game loop might separate instruction execution rate from display rate.
        // For now, tying screen update to timer update (both ~60Hz) is a common approach.
        // The IPS is controlled by delaying after each instruction.

    }

    cleanup(sdl);
    printf("Emulator closed.\n");
    return 0;
}