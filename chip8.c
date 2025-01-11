#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "SDL.h"

// SDL Container
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

// Emulator COnfig
typedef struct {
    uint32_t window_width;
    uint32_t window_height;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t scaling_factor;
    bool outline;
} config_t;

typedef struct {
    uint16_t opcode;
    uint16_t NNN; // 12 bit address constant
    uint8_t NN;
    uint8_t N;
    uint8_t X;
    uint8_t Y;
} instruction_t;

// EMulator States
typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

typedef struct {
    emulator_state_t state;
    uint8_t V[16];
    uint16_t I;
    uint16_t PC;
    uint16_t stack[12];
    uint16_t *stack_pointer;
    bool keypad[16];
    uint8_t ram[4096];
    bool display[64*32];
    uint8_t delay_timer;
    uint8_t sound_timer;
    instruction_t instruction;
    const char *rom_name; // program to emulate
} chip8_t;


// init sdl
bool init_sdl(sdl_t *sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL initialization failed. %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow(
        "CHIP-8", 
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config.window_width * config.scaling_factor,
        config.window_height * config.scaling_factor,
        0);
    
    if (!sdl->window) {
        SDL_Log("SDL window creation failed %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer) {
        SDL_Log("Renderer creation failed %s\n", SDL_GetError());
        return false;
    }

    return true;
}

bool set_config_from_args(config_t *config, int argc, char **argv) {
    // defaults
    *config = (config_t) {
        .window_width = 64,
        .window_height = 32,
        .fg_color = 0x00FFFFFF,
        .bg_color = 0x00000000,
        .scaling_factor = 21,
        .outline = true,
    };

    // override
    for (int i = 1; i < argc; i++) {
        (void)argv[i];
    }

    return true;
}

// Initialize Chip8 mesin
bool init_chip8(chip8_t *chip8, const char rom_name[]) {
    const uint32_t entry_point = 0x200;

    // Font
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

    /* 0xF0 = 1111 0000, 0x90 = 1001 000

    0 will look like
    11110000
    10010000
    10010000
    10010000
    11110000
    */

    memcpy(&chip8->ram[0], font, sizeof(font));

    // rom
    FILE *rom = fopen(rom_name, "rb");
    if (!rom) {
        SDL_Log("Invalid Rom %s\n", rom_name);
        return false;
    }

    // getting the size of the rom
    fseek(rom, 0, SEEK_END);
    const long rom_size = ftell(rom); // size of the rom
    const long max_size = sizeof chip8->ram - entry_point;
    rewind(rom);

    // rom size should ne less than the remaingin space in the ram
    if (rom_size > max_size) {
        SDL_Log("Aaaw itna lamba hai... %s\n", rom_name);
        return false;
    }

    // read the rom file and put it in the ram
    // effectively "loading the program on the ram"
    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1) {
        SDL_Log("Loading ROM to RAM failed lmao... %s\n", rom_name);
        return false;
    }

    fclose(rom);

    // default settings
    chip8->state = RUNNING;
    chip8->PC = entry_point;
    chip8->rom_name = rom_name;
    chip8->stack_pointer = &chip8->stack[0];
    return true;
}

// cleanup
void cleanup(const sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

// set window to bg color
void clear_screen(const sdl_t sdl, config_t config) {
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >>  8) & 0xFF;
    const uint8_t a = (config.bg_color >>  0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8) {
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scaling_factor, .h = config.scaling_factor};
    
    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const uint8_t fg_b = (config.fg_color >>  8) & 0xFF;
    const uint8_t fg_a = (config.fg_color >>  0) & 0xFF;

    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >>  8) & 0xFF;
    const uint8_t bg_a = (config.bg_color >>  0) & 0xFF;

    for (uint32_t i = 0; i <= sizeof(chip8.display); i++) {
        rect.x = (i % config.window_width) * config.scaling_factor;
        rect.y = (i / config.window_width) * config.scaling_factor;

        if (chip8.display[i]) {
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            if (config.outline) {
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        } else {
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }
    
    SDL_RenderPresent(sdl.renderer);
}

// KEyboard press then do things meh
void get_input(chip8_t *chip8) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT; 
                return;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        // esc
                        chip8->state = QUIT;
                        return;

                    case SDLK_SPACE:
                        // spacebat
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;
                            puts("Paused...");

                        } else {
                            chip8->state = RUNNING;
                            puts("Resumed...");
                        }

                    return;
                    
                    default:
                        break;
                }       
                break;

            case SDL_KEYUP:
                break;

            default:
                break;
        }   
    }
}

#ifdef DEBUG
    void print_debug_info(chip8_t *chip8, config_t config) {
        printf("Address: 0x%04X, Opcdoe: 0x%04X Desc: ",
                chip8->PC-2, chip8->instruction.opcode);

        switch((chip8->instruction.opcode >> 12) & 0x0F) {
            case 0x0:
                if (chip8->instruction.NN == 0xE0) {
                    // 0x00E0 -> clear screen
                    printf("Clear Screen!\n");

                } else if (chip8->instruction.NN == 0xE0) {
                    // 0x00EE -> subroutine return
                    printf("Return from subroutine to address 0x%04X\n", 
                            *(chip8->stack_pointer- 1));
                } else {
                    printf("Unimplemented OR Invalid opcode\n");
                }
                break;

            case 0x01:
            // 0x1NNN jump to addr
                chip8->PC = chip8->instruction.NNN;
                printf("JUmp to address NNN 0x%04X\n", chip8->instruction.NNN);
                break;

            case 0x02:
                // 0x2NNN :Call subroutine at NNN
                *chip8->stack_pointer++ = chip8->PC;
                chip8->PC = chip8->instruction.NNN;
                break;

            case 0x0A:
                // 0xANNN: Set index register I to NNN
                printf("Set I to NNN (0x%04X)\n", chip8->instruction.NNN);
                break;

            case 0x06:
            // 0x6XNN: Set register VX += NN
            printf("Set Register V[%X] += NN (0x%02X)\n", 
                    chip8->instruction.X,  chip8->instruction.NN);
                break;

            case 0x07:
            // 0x6XNN: Set register VX += NN
            printf("Set Register V%X (0x%02X) = NN (0x%02X), Result: 0x%02X\n", 
                    chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.NN,
                    chip8->V[chip8->instruction.X] + chip8->instruction.NN);
                break;

        case 0x0D:
            // 0xDXYN: draw N hright sprite at coords X, Y
            // XOR scrren with sprite
            // VF (Carry flag) is set if any scrren pixels are off

            uint8_t X_coord = chip8->V[chip8->instruction.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->instruction.Y] % config.window_height;
            
            const uint8_t orig_X = X_coord;
            
            chip8->V[0xF] = 0;

            for (uint8_t i = 0; i < chip8->instruction.N; i++) {
                // get next row
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X;

                for (int8_t j = 7; j >= 0; j--) {
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    
                    if ((sprite_data & (1 << j)) && *pixel) {
                        chip8->V[0xF] = 1;
                    }

                    // Xor implementation of the above
                    *pixel ^= (sprite_data & (1 << j));

                    // stop drawing if hit right edge
                    if (++X_coord >= config.window_width) break;
    
                }

                if (++Y_coord >= config.window_height) break;

            }

            printf("Draw N (%u) height sprite at coords V%X, (0x%02X), V%X (0x%02X) "
                    "from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",
                    chip8->instruction.N, chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y,
                    chip8->V[chip8->instruction.Y], chip8->I);

            break;


            default:
            printf("Unimplemented OR Invalid opcode\n");
                break;     

        }
    } 

#endif
// Emulate one chip 8 instr

void emulate_instr(chip8_t *chip8, const config_t config) {
    // chip8 needs big indian, we have little indian {endin == indian} feeling paraoud indian amry
    chip8->instruction.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC + 1];
    chip8->PC += 2;

    // ABCD
    chip8->instruction.NNN = chip8->instruction.opcode & 0x0FFF;
    chip8->instruction.NN = chip8->instruction.opcode & 0x0FF;
    chip8->instruction.N = chip8->instruction.opcode & 0x0F;

    chip8->instruction.X = chip8->instruction.opcode >> 8 & 0x0F;
    chip8->instruction.Y = chip8->instruction.opcode >> 4 & 0x0F;

#ifdef DEBUG
    print_debug_info(chip8, config);
#endif

    switch((chip8->instruction.opcode >> 12) & 0x0F) {
        case 0x0:
            if (chip8->instruction.NN == 0xE0) {
                // 0x00E0 -> clear screen
                memset(&chip8->display[0], false, sizeof chip8->display);
            } else if (chip8->instruction.NN == 0xE0) {
                // 0x00EE -> subroutine return
                chip8->PC = *--chip8->stack_pointer;
            }
            break;

        case 0x01:
            // 0x1NNN jump to addr
            chip8->PC = chip8->instruction.NNN;
            break;

        case 0x02:
            // 0x2NNN :Call subroutine at NNN
            *chip8->stack_pointer++ = chip8->PC;
            chip8->PC = chip8->instruction.NNN;
            break;

        case 0x0A:
            // 0xANNN: Set index register I to NNN
            chip8->I = chip8->instruction.NNN;   
            break;

        case 0x06:
            // 0x6XNN: Set register VX to NN
            chip8->V[chip8->instruction.X] = chip8->instruction.NN;
            break;

        case 0x07:
            // 0x6XNN: Set register VX += NN
            chip8->V[chip8->instruction.X] += chip8->instruction.NN;
            break;

        case 0x0D:
            // 0xDXYN: draw N hright sprite at coords X, Y
            // XOR scrren with sprite
            // VF (Carry flag) is set if any scrren pixels are off

            uint8_t X_coord = chip8->V[chip8->instruction.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->instruction.Y] % config.window_height;
            
            const uint8_t orig_X = X_coord;
            
            chip8->V[0xF] = 0;

            for (uint8_t i = 0; i < chip8->instruction.N; i++) {
                // get next row
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X;

                for (int8_t j = 7; j >= 0; j--) {
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    
                    if ((sprite_data & (1 << j)) && *pixel) {
                        chip8->V[0xF] = 1;
                    }

                    // Xor implementation of the above
                    *pixel ^= (sprite_data & (1 << j));

                    // stop drawing if hit right edge
                    if (++X_coord >= config.window_width) break;
    
                }

                if (++Y_coord >= config.window_height) break;

            }
            break;

        default:
            break;
    }
}

int main(int argc, char **argv) {
    // Initializae Configs
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

    // Init SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

    // Init chip8
    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

    // clear screen
    clear_screen(sdl, config);

    // Main Loop
    while (chip8.state != QUIT) {

        get_input(&chip8);

        // busy waiting
        if (chip8.state == PAUSED) {
            continue;
        }

        // Emulaate Chip8 instructions
        emulate_instr(&chip8, config);

        // 60 fps delay
        SDL_Delay(16);

        update_screen(sdl, config, chip8);
    }

    // Cleanup stuff
    cleanup(sdl);

    puts("Smartest Programmer Ever!");
}
