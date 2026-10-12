#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdarg.h>
#include <regex.h>
#include <stdint.h>

#define MAX_LINE_LEN 4096
#define MAX_ARG_LEN 4096
#define HASH_STR_LEN 9
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_RESET "\033[0m"

static bool debug_enabled = false;

/* Flawfinder: ignore - gcc format attribute */
__attribute__((unused, format(printf, 1, 2)))
static void debug(const char* format, ...)
{
    if (!debug_enabled)
        return;

    va_list args;
    va_start(args, format);
    fprintf(stderr, "%sDebug: ", COLOR_YELLOW);
    /* Flawfinder: ignore - format string supplied internally */
    vfprintf(stderr, format, args);
    fprintf(stderr, "%s", COLOR_RESET);
    va_end(args);
}

typedef struct Pattern
{
    regex_t pattern;
    int8_t groups;
    bool is_initialized;
} Pattern;

typedef struct Patterns
{
    Pattern vm_binary;
    Pattern dump_processor_state;
    Pattern dump_register_state;
    Pattern li;
    Pattern add;
    Pattern sub;
    Pattern addi;
    Pattern addu;
    Pattern subu;
    Pattern addiu;
    Pattern mul;
    Pattern mult;
    Pattern div;
    Pattern and;
    Pattern or;
    Pattern andi;
    Pattern ori;
    Pattern sll;
    Pattern srl;
    Pattern xor;
    Pattern snapshot;
    Pattern validate_instructions;
    Pattern skip_instructions;
} Patterns;

typedef struct Match
{
    bool is_match;
    bool has_error;
    const char* original;
    char** capture_groups;
} Match;

static void teardown_match(Match* match)
{
    if (match == NULL)
        return;

    if (match->capture_groups != NULL)
    {
        for (size_t i = 0; match->capture_groups[i] != NULL; i++)
        {
            free(match->capture_groups[i]);
            match->capture_groups[i] = NULL;
        }
        free(match->capture_groups);
        match->capture_groups = NULL;
    }

    memset(match, 0, sizeof(Match));
    free(match);
}

static bool match_pattern(const char* string, const Pattern* pattern, Match* match)
{
    if (string == NULL || pattern == NULL || match == NULL || match->has_error)
        return false;

    regmatch_t matched_groups[pattern->groups + 1];

    if (regexec(&pattern->pattern, string, pattern->groups + 1, matched_groups, 0) != 0)
        return false;

    match->is_match = true;
    match->original = string;

    if (pattern->groups > 0)
    {
        match->capture_groups = calloc(pattern->groups + 1, sizeof(char*));

        if (match->capture_groups == NULL)
        {
            fprintf(stderr, "Error: Match->capture_groups memory allocation failed.\n");
            match->has_error = true;
            return false;
        }

        for (size_t i = 0; i < pattern->groups; i++)
        {
            size_t group_idx = i + 1;

            if (matched_groups[group_idx].rm_so == -1 || matched_groups[group_idx].rm_eo == -1)
            {
                match->capture_groups[i] = NULL;
                break;
            }

            const size_t match_len = matched_groups[group_idx].rm_eo - matched_groups[group_idx].rm_so;
            match->capture_groups[i] = strndup(string + matched_groups[group_idx].rm_so, match_len);
        }
    }

    return true;
}

static bool initialize_match(Match** match)
{
    if (match == NULL)
        return false;

    if (*match == NULL)
    {
        *match = calloc(1, sizeof(Match));
        if (*match == NULL)
        {
            fprintf(stderr, "Error: Match memory allocation failed.\n");
            return false;
        }
        (*match)->has_error = false;
        return true;
    }

    if ((*match)->has_error)
        return false;

    (*match)->is_match = false;
    (*match)->original = NULL;

    if ((*match)->capture_groups != NULL)
    {
        for (size_t i = 0; (*match)->capture_groups[i] != NULL; i++)
        {
            free((*match)->capture_groups[i]);
            (*match)->capture_groups[i] = NULL;
        }
        free((*match)->capture_groups);
        (*match)->capture_groups = NULL;
    }

    return true;
}

typedef struct NodeConfig
{
    char* assembly_file_name;
    char* vm_binary;
    char* snapshot_file;
} NodeConfig;

typedef struct NodeSocket
{
    FILE* file;
    bool has_pending_instructions;
} NodeSocket;

typedef struct NodeProcessor
{
    /*
     * registers[32] == $hi
     * registers[33] == $low
     */
    int32_t registers[34];
    int32_t executed_instructions;
} NodeProcessor;

typedef struct Node
{
    NodeConfig config;
    NodeSocket socket;
    NodeProcessor processor;
    struct Node* next;
} Node;

typedef struct Hypervisor
{
    Node* head_node;
    Node* current_node;
    Patterns patterns;
    unsigned int active_nodes;
} Hypervisor;

static bool get_register_key(const char* raw_key, int8_t* key)
{
    if (raw_key == NULL)
        return false;

    if (strcmp(raw_key, "hi") == 0)
    {
        *key = 32;
        return true;
    }

    if (strcmp(raw_key, "low") == 0)
    {
        *key = 33;
        return true;
    }

    char* endptr = NULL;
    const long converted_key = strtol(raw_key, &endptr, 10);

    if (endptr == raw_key || *endptr != '\0' || converted_key < 0 || converted_key > 31)
    {
        fprintf(stderr, "Exception: Invalid register \"$%s\"\n", raw_key);

        return false;
    }

    *key = (int8_t)converted_key;

    return true;
}

static bool get_register(const NodeProcessor* processor, const char* raw_key, int32_t* value)
{
    int8_t key;

    if (!get_register_key(raw_key, &key))
        return false;

    *value = processor->registers[key];
    return true;
}

static void set_register(NodeProcessor* processor, const char* raw_key, int32_t value)
{
    int8_t key;

    if (!get_register_key(raw_key, &key))
    {
        return;
    }

    if (key == 0)
    {
        return;
    }

    processor->registers[key] = value;
    debug("NodeProcessor->registers[%d] = %d\n", key, processor->registers[key]);
}

static void dump_processor_state(const NodeProcessor* processor)
{
    for (int i = 0; i < 32; i++)
    {
        fprintf(stdout, "R%d=%d\n", i, processor->registers[i]);
    }

    fprintf(stdout, "Rhi=%d\n", processor->registers[32]);
    fprintf(stdout, "Rlow=%d\n", processor->registers[33]);
}

static void dump_register_state(const NodeProcessor* processor, const Match* match)
{
    int32_t value;

    if (get_register(processor, match->capture_groups[0], &value))
    {
        fprintf(stdout, "R%s=%d\n", match->capture_groups[0], value);
    }
}

static void load_immediate(Node* node, const Match* match)
{
    set_register(&node->processor, match->capture_groups[0], (int32_t)strtol(match->capture_groups[1], NULL, 10));
}

static void add(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[1], &value_1) || !get_register(
        processor, match->capture_groups[2], &value_2))
    {
        return;
    }

    const int64_t sum = (int64_t)value_1 + (int64_t)value_2;

    if (sum < INT32_MIN || sum > INT32_MAX)
    {
        fprintf(stderr, "Exception: Integer overflow in \"%s\"\n", match->original);
        return;
    }

    set_register(processor, match->capture_groups[0], (int32_t)sum);
}

static void subtract(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[1], &value_1) || !get_register(
        processor, match->capture_groups[2], &value_2))
    {
        return;
    }

    const int64_t difference = (int64_t)value_1 - (int64_t)value_2;

    if (difference < INT32_MIN || difference > INT32_MAX)
    {
        fprintf(stderr, "Exception: Integer overflow in \"%s\"\n", match->original);
        return;
    }

    set_register(processor, match->capture_groups[0], (int32_t)difference);
}

static void add_immediate(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;

    if (!get_register(processor, match->capture_groups[1], &value_1))
    {
        return;
    }

    const int64_t sum = (int64_t)value_1 + (int64_t)strtol(match->capture_groups[2], NULL, 10);

    if (sum < INT32_MIN || sum > INT32_MAX)
    {
        fprintf(stderr, "Exception: Integer overflow in \"%s\"\n", match->original);
        return;
    }

    set_register(processor, match->capture_groups[0], (int32_t)sum);
}

static void add_unsigned(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[1], &value_1) || !get_register(
        processor, match->capture_groups[2], &value_2))
    {
        return;
    }

    set_register(processor, match->capture_groups[0], (int32_t)((uint32_t)value_1 + (uint32_t)value_2));
}

static void subtract_unsigned(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[1], &value_1) || !get_register(
        processor, match->capture_groups[2], &value_2))
    {
        return;
    }

    set_register(processor, match->capture_groups[0], (int32_t)((uint32_t)value_1 - (uint32_t)value_2));
}

static void add_immediate_unsigned(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;

    if (!get_register(processor, match->capture_groups[1], &value_1))
    {
        return;
    }

    const int32_t value_2 = (int32_t)strtol(match->capture_groups[2], NULL, 10);
    set_register(processor, match->capture_groups[0], (int32_t)((uint32_t)value_1 + (uint32_t)value_2));
}

static void multiply_without_overflow(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[1], &value_1) || !get_register(
        processor, match->capture_groups[2], &value_2))
    {
        return;
    }

    set_register(processor, match->capture_groups[0], (int32_t)((uint32_t)value_1 * (uint32_t)value_2));
}

static void multiply(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[0], &value_1) || !get_register(
        processor, match->capture_groups[1], &value_2))
    {
        return;
    }

    const int64_t product = (int64_t)value_1 * (int64_t)value_2;
    const uint64_t unsigned_product = (uint64_t)product;

    set_register(processor, "hi", (int32_t)(unsigned_product >> 32));
    set_register(processor, "low", (int32_t)(unsigned_product & 0xFFFFFFFF));
}

static void divide(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[0], &value_1) || !get_register(
        processor, match->capture_groups[1], &value_2))
    {
        return;
    }

    if (value_2 == 0)
    {
        fprintf(stderr, "Exception: Division by zero in \"%s\"\n", match->original);
        return;
    }

    if (value_1 == INT32_MIN && value_2 == -1)
    {
        set_register(processor, "hi", 0);
        set_register(processor, "low", INT32_MIN);
        return;
    }

    set_register(processor, "hi", value_1 % value_2);
    set_register(processor, "low", value_1 / value_2);
}

static void and(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[1], &value_1) || !get_register(
        processor, match->capture_groups[2], &value_2))
    {
        return;
    }

    set_register(processor, match->capture_groups[0], value_1 & value_2);
}

static void or(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[1], &value_1) || !get_register(
        processor, match->capture_groups[2], &value_2))
    {
        return;
    }

    set_register(processor, match->capture_groups[0], value_1 | value_2);
}

static void xor(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;
    int32_t value_2;

    if (!get_register(processor, match->capture_groups[1], &value_1) || !get_register(
        processor, match->capture_groups[2], &value_2))
    {
        return;
    }

    set_register(processor, match->capture_groups[0], value_1 ^ value_2);
}

static void and_immediate(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;

    if (!get_register(processor, match->capture_groups[1], &value_1))
    {
        return;
    }

    const int32_t value_2 = (int32_t)strtol(match->capture_groups[2], NULL, 10);
    set_register(processor, match->capture_groups[0], value_1 & value_2);
}

static void or_immediate(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;

    if (!get_register(processor, match->capture_groups[1], &value_1))
    {
        return;
    }

    const int32_t value_2 = (int32_t)strtol(match->capture_groups[2], NULL, 10);
    set_register(processor, match->capture_groups[0], value_1 | value_2);
}

static void shift_left_logical(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;

    if (!get_register(processor, match->capture_groups[1], &value_1))
    {
        return;
    }

    const int32_t value_2 = (int32_t)strtol(match->capture_groups[2], NULL, 10);
    set_register(processor, match->capture_groups[0], (int32_t)((uint32_t)value_1 << (value_2 & 0x1F)));
}

static void shift_right_logical(NodeProcessor* processor, const Match* match)
{
    int32_t value_1;

    if (!get_register(processor, match->capture_groups[1], &value_1))
    {
        return;
    }

    const int32_t value_2 = (int32_t)strtol(match->capture_groups[2], NULL, 10);
    set_register(processor, match->capture_groups[0], (int32_t)((uint32_t)value_1 >> (value_2 & 0x1F)));
}

// Checks whether a line is empty after stripping line breaks to allow skipping blank lines.
static bool is_blank_line(const char* line)
{
    return line == NULL || line[0] == '\0';
}

// See https://gist.github.com/sgsfak/9ba382a0049f6ee885f68621ae86079b#file-hashes-c-L37-L58.
static bool hash_file(const char* file_path, char* output_hash)
{
    if (file_path == NULL || output_hash == NULL)
        return false;

    /* Flawfinder: ignore - file name validated/supplied internally */
    FILE* file = fopen(file_path, "rb");

    if (file == NULL)
        return false;

    const uint32_t fnv_32_prime = 0x01000193;
    uint32_t hash = 0x811c9dc5;
    uint8_t buffer[4096];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        for (size_t i = 0; i < bytes_read; i++)
        {
            hash ^= (uint32_t)buffer[i];
            hash *= fnv_32_prime;
        }
    }

    fclose(file);
    snprintf(output_hash, HASH_STR_LEN, "%08x", hash);
    return true;
}

static void take_snapshot(const Node* node, const char* snapshot_path)
{
    if (node == NULL || snapshot_path == NULL)
        return;

    debug("Taking snapshot \"%s\"\n", snapshot_path);

    /* Flawfinder: ignore - snapshot path supplied by instruction */
    FILE* file = fopen(snapshot_path, "w");

    if (file == NULL)
    {
        fprintf(stderr, "Exception: Failed to create snapshot \"%s\"\n", snapshot_path);
        return;
    }

    /* Flawfinder: ignore - fixed size FNV-1 32-bit hex hash string */
    char hash[HASH_STR_LEN] = {0};

    if (!hash_file(node->config.vm_binary, hash))
    {
        fclose(file);
        fprintf(stderr, "Exception: Failed to create snapshot \"%s\"\n", snapshot_path);
        return;
    }

    fprintf(file, "VALIDATE_INSTRUCTIONS=%s\n", hash);
    fprintf(file, "SKIP_INSTRUCTIONS=%d\n", node->processor.executed_instructions + 1);

    for (int i = 0; i < 32; i++)
    {
        if (node->processor.registers[i] != 0)
            fprintf(file, "li $%d,%d\n", i, node->processor.registers[i]);
    }

    if (node->processor.registers[32] != 0)
        fprintf(file, "li $hi,%d\n", node->processor.registers[32]);

    if (node->processor.registers[33] != 0)
        fprintf(file, "li $low,%d\n", node->processor.registers[33]);

    fclose(file);
}

static void skip_instructions(Node* node, const Match* match)
{
    const long skip_count = strtol(match->capture_groups[0], NULL, 10);
    char* skip_line = NULL;
    size_t skip_len = 0;
    long skipped = 0;

    debug("Skipping %ld instructions\n", skip_count);

    while (skipped < skip_count)
    {
        if (getline(&skip_line, &skip_len, node->socket.file) == -1)
            break;

        skip_line[strcspn(skip_line, "\r\n")] = '\0';

        if (is_blank_line(skip_line))
            continue;

        skipped++;
    }

    if (skip_line != NULL)
        free(skip_line);

    node->processor.executed_instructions = (int32_t)skip_count;
}

static bool run_node(Hypervisor* hypervisor)
{
    ssize_t chars_read;
    char* line = NULL;
    size_t len = 0;
    Match* match = NULL;
    bool has_error = false;

    while ((chars_read = getline(&line, &len, hypervisor->current_node->socket.file)) != -1)
    {
        line[strcspn(line, "\r\n")] = '\0'; // Remove line breaks.

        if (is_blank_line(line))
            continue;

        debug("Running instruction \"%s\"\n", line);

        if (!initialize_match(&match))
        {
            has_error = true;
            break;
        }

        if (match_pattern(line, &hypervisor->patterns.dump_processor_state, match))
            dump_processor_state(&hypervisor->current_node->processor);
        else if (match_pattern(line, &hypervisor->patterns.dump_register_state, match))
            dump_register_state(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.li, match))
            load_immediate(hypervisor->current_node, match);
        else if (match_pattern(line, &hypervisor->patterns.add, match))
            add(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.sub, match))
            subtract(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.addi, match))
            add_immediate(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.addu, match))
            add_unsigned(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.subu, match))
            subtract_unsigned(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.addiu, match))
            add_immediate_unsigned(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.mul, match))
            multiply_without_overflow(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.mult, match))
            multiply(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.div, match))
            divide(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.and, match))
            and(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.or, match))
            or(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.andi, match))
            and_immediate(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.ori, match))
            or_immediate(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.sll, match))
            shift_left_logical(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.srl, match))
            shift_right_logical(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.xor, match))
            xor(&hypervisor->current_node->processor, match);
        else if (match_pattern(line, &hypervisor->patterns.snapshot, match))
            take_snapshot(hypervisor->current_node, match->capture_groups[0]);
        else
            fprintf(stderr, "Exception: Unknown instruction \"%s\"\n", line);

        hypervisor->current_node->processor.executed_instructions += 1;
    }

    if (chars_read == -1)
        hypervisor->current_node->socket.has_pending_instructions = false;

    teardown_match(match);

    if (line != NULL)
        free(line);

    return !has_error;
}

static void debug_node(Hypervisor* hypervisor)
{
    debug("Hypervisor->current_node = \"%s\"\n", hypervisor->current_node->config.vm_binary);
}

static void switch_node(Hypervisor* hypervisor)
{
    hypervisor->current_node = (hypervisor->current_node->next != NULL)
                                   ? hypervisor->current_node->next
                                   : hypervisor->head_node;

    debug_node(hypervisor);
}

static bool run_hypervisor(Hypervisor* hypervisor)
{
    if (hypervisor == NULL)
        return false;

    if (hypervisor->head_node == NULL)
        return true;

    debug_node(hypervisor);

    while (hypervisor->active_nodes > 0)
    {
        if (!hypervisor->current_node->socket.has_pending_instructions)
        {
            switch_node(hypervisor);
            continue;
        }

        if (!run_node(hypervisor))
            return false;

        if (!hypervisor->current_node->socket.has_pending_instructions)
            hypervisor->active_nodes -= 1;

        switch_node(hypervisor);
    }

    return true;
}

static void reset_node_processor(NodeProcessor* processor)
{
    if (processor == NULL)
        return;

    for (int i = 0; i < 34; i++)
    {
        processor->registers[i] = 0;
    }

    processor->executed_instructions = 0;
}

static void teardown_node_socket(NodeSocket* socket)
{
    if (socket == NULL)
        return;

    if (socket->file != NULL)
    {
        fclose(socket->file);
        socket->file = NULL;
    }

    socket->has_pending_instructions = false;
}

static bool setup_node_socket(char* vm_binary, NodeSocket* socket)
{
    memset(socket, 0, sizeof(NodeSocket));

    if (vm_binary == NULL)
    {
        fprintf(stderr, "Error: vm_binary was not configured.\n");
        return false;
    }

    /* Flawfinder: ignore - file name supplied by user. */
    socket->file = fopen(vm_binary, "r");

    if (socket->file == NULL)
    {
        fprintf(stderr, "Error: vm_binary \"%s\" cannot be opened.\n", vm_binary);
        return false;
    }

    socket->has_pending_instructions = true;

    return true;
}

static void teardown_node_config(NodeConfig* config)
{
    if (config == NULL)
        return;

    if (config->assembly_file_name != NULL)
    {
        memset(config->assembly_file_name, 0, strnlen(config->assembly_file_name, MAX_ARG_LEN));
        free(config->assembly_file_name);
        config->assembly_file_name = NULL;
    }

    if (config->vm_binary != NULL)
    {
        memset(config->vm_binary, 0, strnlen(config->vm_binary, MAX_LINE_LEN));
        free(config->vm_binary);
        config->vm_binary = NULL;
    }

    if (config->snapshot_file != NULL)
    {
        memset(config->snapshot_file, 0, strnlen(config->snapshot_file, MAX_LINE_LEN));
        free(config->snapshot_file);
        config->snapshot_file = NULL;
    }

    memset(config, 0, sizeof(NodeConfig));
}

static bool setup_node_config(char* assembly_file_name, NodeConfig* config, const Patterns* patterns)
{
    memset(config, 0, sizeof(NodeConfig));
    config->assembly_file_name = strndup(assembly_file_name, MAX_LINE_LEN);
    debug("NodeConfig->assembly_file_name = \"%s\"\n", config->assembly_file_name);

    if (config->assembly_file_name == NULL)
    {
        fprintf(stderr, "Error: NodeConfig.assembly_file_name memory allocation failed.\n");
        return false;
    }

    /* Flawfinder: ignore - file name supplied by user. */
    FILE* assembly_file = fopen(assembly_file_name, "r");

    if (assembly_file == NULL)
    {
        fprintf(stderr, "Error: configuration \"%s\" cannot be opened.\n", assembly_file_name);
        teardown_node_config(config);
        return false;
    }

    char* line = NULL;
    size_t len = 0;
    bool has_error = false;
    Match* match = NULL;

    while (getline(&line, &len, assembly_file) != -1)
    {
        if (!initialize_match(&match))
        {
            has_error = true;
            break;
        }

        if (match_pattern(line, &patterns->vm_binary, match))
        {
            if (config->vm_binary != NULL)
                free(config->vm_binary);

            config->vm_binary = strndup(match->capture_groups[0], MAX_LINE_LEN);
            debug("NodeConfig->vm_binary = \"%s\"\n", config->vm_binary);

            if (config->vm_binary == NULL)
            {
                fprintf(stderr, "Error: NodeConfig.vm_binary memory allocation failed.\n");
                has_error = true;
            }
        }

        if (has_error)
            break;
    }

    fclose(assembly_file);
    teardown_match(match);

    if (line != NULL)
        free(line);

    if (has_error)
    {
        teardown_node_config(config);
        return false;
    }

    return true;
}

static void teardown_node(Node* node)
{
    if (node == NULL)
        return;

    teardown_node_config(&node->config);
    teardown_node_socket(&node->socket);
    reset_node_processor(&node->processor);

    memset(node, 0, sizeof(Node));
    free(node);
}

static void drop_node(Hypervisor* hypervisor, Node* target_node)
{
    if (hypervisor == NULL || target_node == NULL)
        return;

    Node* prev = NULL;
    Node* current = hypervisor->head_node;

    while (current != NULL && current != target_node)
    {
        prev = current;
        current = current->next;
    }

    if (current == target_node)
    {
        debug("Dropping node \"%s\"\n", target_node->config.vm_binary);

        if (prev == NULL)
            hypervisor->head_node = target_node->next;
        else
            prev->next = target_node->next;

        hypervisor->active_nodes -= 1;
        teardown_node(target_node);
    }
}

static bool validate_snapshot(FILE* snapshot_file, const Node* node, const Patterns* patterns)
{
    if (snapshot_file == NULL || node == NULL || patterns == NULL)
        return false;

    debug("Validating snapshot \"%s\"\n", node->config.snapshot_file);

    /* Flawfinder: ignore - fixed size FNV-1 32-bit hex hash string */
    char expected_hash[HASH_STR_LEN] = {0};

    if (!hash_file(node->config.vm_binary, expected_hash))
    {
        fprintf(stderr, "Error: Failed to hash instruction file \"%s\".\n", node->config.vm_binary);
        return false;
    }

    char* line = NULL;
    size_t len = 0;
    Match* match = NULL;
    bool has_validate_instruction = false;
    bool is_hash_matched = false;
    bool is_skip_validated = false;

    while (getline(&line, &len, snapshot_file) != -1)
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (is_blank_line(line))
            continue;

        if (!initialize_match(&match))
            break;

        if (!has_validate_instruction)
        {
            if (match_pattern(line, &patterns->validate_instructions, match))
            {
                has_validate_instruction = true;

                if (strcmp(match->capture_groups[0], expected_hash) == 0)
                {
                    is_hash_matched = true;
                    continue;
                }
            }

            break;
        }

        if (match_pattern(line, &patterns->skip_instructions, match))
            is_skip_validated = true;

        break;
    }

    teardown_match(match);

    if (line != NULL)
        free(line);

    if (!has_validate_instruction)
        fprintf(stderr,
                "Error: Snapshot validation failed for \"%s\". Missing or invalid VALIDATE_INSTRUCTIONS.\n",
                node->config.snapshot_file);
    else if (!is_hash_matched)
        fprintf(stderr,
                "Error: Snapshot validation failed for \"%s\". Instructions from where snapshot was generated does not "
                "match current instructions.\n",
                node->config.snapshot_file);
    else if (!is_skip_validated)
        fprintf(stderr,
                "Error: Snapshot validation failed for \"%s\". Missing or invalid SKIP_INSTRUCTIONS.\n",
                node->config.snapshot_file);

    rewind(snapshot_file);

    return has_validate_instruction && is_hash_matched && is_skip_validated;
}

static bool restore_node(Hypervisor* hypervisor)
{
    if (hypervisor == NULL || hypervisor->current_node == NULL || hypervisor->current_node->config.snapshot_file ==
        NULL)
        return false;

    Node* node = hypervisor->current_node;
    /* Flawfinder: ignore - snapshot file supplied by user */
    FILE* snapshot_file = fopen(node->config.snapshot_file, "r");

    if (snapshot_file == NULL)
    {
        fprintf(stderr, "Error: Snapshot file \"%s\" cannot be opened.\n", node->config.snapshot_file);
        return false;
    }

    debug("Restoring node from \"%s\"\n", node->config.snapshot_file);

    if (!validate_snapshot(snapshot_file, node, &hypervisor->patterns))
    {
        fclose(snapshot_file);
        return false;
    }

    char* line = NULL;
    size_t len = 0;
    Match* match = NULL;
    bool has_error = false;

    while (getline(&line, &len, snapshot_file) != -1)
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (is_blank_line(line))
            continue;

        if (!initialize_match(&match))
        {
            has_error = true;
            break;
        }

        if (match_pattern(line, &hypervisor->patterns.validate_instructions, match))
            continue;
        else if (match_pattern(line, &hypervisor->patterns.skip_instructions, match))
            skip_instructions(node, match);
        else if (match_pattern(line, &hypervisor->patterns.li, match))
            load_immediate(node, match);
        else
            fprintf(stderr, "Exception: Unknown snapshot instruction \"%s\"\n", line);
    }

    teardown_match(match);
    fclose(snapshot_file);

    if (line != NULL)
        free(line);

    return !has_error;
}

static Node* setup_node(char* assembly_file_name, const Patterns* patterns)
{
    Node* node = calloc(1, sizeof(Node));

    if (node == NULL)
    {
        fprintf(stderr, "Error: Node memory allocation failed.\n");
        return NULL;
    }

    if (!setup_node_config(assembly_file_name, &node->config, patterns))
    {
        teardown_node(node);
        return NULL;
    }

    if (!setup_node_socket(node->config.vm_binary, &node->socket))
    {
        teardown_node(node);
        return NULL;
    }

    reset_node_processor(&node->processor);

    return node;
}

static void teardown_hypervisor_patterns(Patterns* patterns)
{
    if (patterns == NULL)
        return;

    if (patterns->vm_binary.is_initialized)
    {
        regfree(&patterns->vm_binary.pattern);
        patterns->vm_binary.groups = 0;
        patterns->vm_binary.is_initialized = false;
    }

    if (patterns->dump_processor_state.is_initialized)
    {
        regfree(&patterns->dump_processor_state.pattern);
        patterns->dump_processor_state.groups = 0;
        patterns->dump_processor_state.is_initialized = false;
    }

    if (patterns->dump_register_state.is_initialized)
    {
        regfree(&patterns->dump_register_state.pattern);
        patterns->dump_register_state.groups = 0;
        patterns->dump_register_state.is_initialized = false;
    }

    if (patterns->li.is_initialized)
    {
        regfree(&patterns->li.pattern);
        patterns->li.groups = 0;
        patterns->li.is_initialized = false;
    }

    if (patterns->add.is_initialized)
    {
        regfree(&patterns->add.pattern);
        patterns->add.groups = 0;
        patterns->add.is_initialized = false;
    }

    if (patterns->sub.is_initialized)
    {
        regfree(&patterns->sub.pattern);
        patterns->sub.groups = 0;
        patterns->sub.is_initialized = false;
    }

    if (patterns->addi.is_initialized)
    {
        regfree(&patterns->addi.pattern);
        patterns->addi.groups = 0;
        patterns->addi.is_initialized = false;
    }

    if (patterns->addu.is_initialized)
    {
        regfree(&patterns->addu.pattern);
        patterns->addu.groups = 0;
        patterns->addu.is_initialized = false;
    }

    if (patterns->subu.is_initialized)
    {
        regfree(&patterns->subu.pattern);
        patterns->subu.groups = 0;
        patterns->subu.is_initialized = false;
    }

    if (patterns->addiu.is_initialized)
    {
        regfree(&patterns->addiu.pattern);
        patterns->addiu.groups = 0;
        patterns->addiu.is_initialized = false;
    }

    if (patterns->mul.is_initialized)
    {
        regfree(&patterns->mul.pattern);
        patterns->mul.groups = 0;
        patterns->mul.is_initialized = false;
    }

    if (patterns->mult.is_initialized)
    {
        regfree(&patterns->mult.pattern);
        patterns->mult.groups = 0;
        patterns->mult.is_initialized = false;
    }

    if (patterns->div.is_initialized)
    {
        regfree(&patterns->div.pattern);
        patterns->div.groups = 0;
        patterns->div.is_initialized = false;
    }

    if (patterns->and.is_initialized)
    {
        regfree(&patterns->and.pattern);
        patterns->and.groups = 0;
        patterns->and.is_initialized = false;
    }

    if (patterns->or.is_initialized)
    {
        regfree(&patterns->or.pattern);
        patterns->or.groups = 0;
        patterns->or.is_initialized = false;
    }

    if (patterns->andi.is_initialized)
    {
        regfree(&patterns->andi.pattern);
        patterns->andi.groups = 0;
        patterns->andi.is_initialized = false;
    }

    if (patterns->ori.is_initialized)
    {
        regfree(&patterns->ori.pattern);
        patterns->ori.groups = 0;
        patterns->ori.is_initialized = false;
    }

    if (patterns->sll.is_initialized)
    {
        regfree(&patterns->sll.pattern);
        patterns->sll.groups = 0;
        patterns->sll.is_initialized = false;
    }

    if (patterns->srl.is_initialized)
    {
        regfree(&patterns->srl.pattern);
        patterns->srl.groups = 0;
        patterns->srl.is_initialized = false;
    }

    if (patterns->xor.is_initialized)
    {
        regfree(&patterns->xor.pattern);
        patterns->xor.groups = 0;
        patterns->xor.is_initialized = false;
    }

    if (patterns->snapshot.is_initialized)
    {
        regfree(&patterns->snapshot.pattern);
        patterns->snapshot.groups = 0;
        patterns->snapshot.is_initialized = false;
    }

    if (patterns->validate_instructions.is_initialized)
    {
        regfree(&patterns->validate_instructions.pattern);
        patterns->validate_instructions.groups = 0;
        patterns->validate_instructions.is_initialized = false;
    }

    if (patterns->skip_instructions.is_initialized)
    {
        regfree(&patterns->skip_instructions.pattern);
        patterns->skip_instructions.groups = 0;
        patterns->skip_instructions.is_initialized = false;
    }
}

static bool fail_setup_hypervisor_pattern(const char* failed_pattern, Patterns* patterns)
{
    fprintf(stderr, "Error: Failed to compile \"%s\" regex pattern.\n", failed_pattern);
    teardown_hypervisor_patterns(patterns);
    return false;
}

static bool setup_hypervisor_patterns(Patterns* patterns)
{
    if (patterns == NULL)
        return false;

    memset(patterns, 0, sizeof(Patterns));

    if (regcomp(&patterns->vm_binary.pattern,
                "^[[:space:]]*vm_binary[[:space:]]*=[[:space:]]*([^[:space:]]+)[[:space:]]*$", REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("vm_binary", patterns);
    patterns->vm_binary.groups = 1;
    patterns->vm_binary.is_initialized = true;

    if (regcomp(&patterns->dump_processor_state.pattern, "^[[:space:]]*DUMP_PROCESSOR_STATE[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("dump_processor_state", patterns);
    patterns->dump_processor_state.groups = 0;
    patterns->dump_processor_state.is_initialized = true;

    if (regcomp(&patterns->dump_register_state.pattern,
                "^[[:space:]]*DUMP_REGISTER_STATE[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("dump_register_state", patterns);
    patterns->dump_register_state.groups = 1;
    patterns->dump_register_state.is_initialized = true;

    if (regcomp(&patterns->li.pattern,
                "^[[:space:]]*li[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*([-+]{0,1}[0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("li", patterns);
    patterns->li.groups = 2;
    patterns->li.is_initialized = true;

    if (regcomp(&patterns->add.pattern,
                "^[[:space:]]*add[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("add", patterns);
    patterns->add.groups = 3;
    patterns->add.is_initialized = true;

    if (regcomp(&patterns->sub.pattern,
                "^[[:space:]]*sub[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("sub", patterns);
    patterns->sub.groups = 3;
    patterns->sub.is_initialized = true;

    if (regcomp(&patterns->addi.pattern,
                "^[[:space:]]*addi[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*([-+]{0,1}[0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("addi", patterns);
    patterns->addi.groups = 3;
    patterns->addi.is_initialized = true;

    if (regcomp(&patterns->addu.pattern,
                "^[[:space:]]*addu[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("addu", patterns);
    patterns->addu.groups = 3;
    patterns->addu.is_initialized = true;

    if (regcomp(&patterns->subu.pattern,
                "^[[:space:]]*subu[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("subu", patterns);
    patterns->subu.groups = 3;
    patterns->subu.is_initialized = true;

    if (regcomp(&patterns->addiu.pattern,
                "^[[:space:]]*addiu[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*([-+]{0,1}[0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("addiu", patterns);
    patterns->addiu.groups = 3;
    patterns->addiu.is_initialized = true;

    if (regcomp(&patterns->mul.pattern,
                "^[[:space:]]*mul[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("mul", patterns);
    patterns->mul.groups = 3;
    patterns->mul.is_initialized = true;

    if (regcomp(&patterns->mult.pattern,
                "^[[:space:]]*mult[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("mult", patterns);
    patterns->mult.groups = 2;
    patterns->mult.is_initialized = true;

    if (regcomp(&patterns->div.pattern,
                "^[[:space:]]*div[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("div", patterns);
    patterns->div.groups = 2;
    patterns->div.is_initialized = true;

    if (regcomp(&patterns->and.pattern,
                "^[[:space:]]*and[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("and", patterns);
    patterns->and.groups = 3;
    patterns->and.is_initialized = true;

    if (regcomp(&patterns->or.pattern,
                "^[[:space:]]*or[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("or", patterns);
    patterns->or.groups = 3;
    patterns->or.is_initialized = true;

    if (regcomp(&patterns->andi.pattern,
                "^[[:space:]]*andi[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*([-+]{0,1}[0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("andi", patterns);
    patterns->andi.groups = 3;
    patterns->andi.is_initialized = true;

    /* Both "or" and "ori" are supported because the name is "or" in the MIPS Instruction Set document.
     * See https://github.com/ianbunag/cmpe-283-virtual-technologies/blob/main/project/doc/MIPS_Instruction_Set.pdf).
     */
    if (regcomp(&patterns->ori.pattern,
                "^[[:space:]]*ori?[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*([-+]{0,1}[0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("ori", patterns);
    patterns->ori.groups = 3;
    patterns->ori.is_initialized = true;

    if (regcomp(&patterns->sll.pattern,
                "^[[:space:]]*sll[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("sll", patterns);
    patterns->sll.groups = 3;
    patterns->sll.is_initialized = true;

    if (regcomp(&patterns->srl.pattern,
                "^[[:space:]]*srl[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("srl", patterns);
    patterns->srl.groups = 3;
    patterns->srl.is_initialized = true;

    if (regcomp(&patterns->xor.pattern,
                "^[[:space:]]*xor[[:space:]]+\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*,[[:space:]]*\\$([0-9]+|hi|low)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("xor", patterns);
    patterns->xor.groups = 3;
    patterns->xor.is_initialized = true;

    if (regcomp(&patterns->snapshot.pattern,
                "^[[:space:]]*SNAPSHOT[[:space:]]+([^[:space:]]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("snapshot", patterns);
    patterns->snapshot.groups = 1;
    patterns->snapshot.is_initialized = true;

    if (regcomp(&patterns->validate_instructions.pattern,
                "^[[:space:]]*VALIDATE_INSTRUCTIONS[[:space:]]*=[[:space:]]*([a-fA-F0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("validate_instructions", patterns);
    patterns->validate_instructions.groups = 1;
    patterns->validate_instructions.is_initialized = true;

    if (regcomp(&patterns->skip_instructions.pattern,
                "^[[:space:]]*SKIP_INSTRUCTIONS[[:space:]]*=[[:space:]]*([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("skip_instructions", patterns);
    patterns->skip_instructions.groups = 1;
    patterns->skip_instructions.is_initialized = true;

    return true;
}

static void teardown_hypervisor(Hypervisor* hypervisor)
{
    if (hypervisor == NULL)
        return;

    teardown_hypervisor_patterns(&hypervisor->patterns);

    Node* current_node = hypervisor->head_node;
    hypervisor->head_node = NULL;
    hypervisor->active_nodes = 0;

    while (current_node != NULL)
    {
        Node* next = current_node->next;
        teardown_node(current_node);
        current_node = next;
    }

    memset(hypervisor, 0, sizeof(Hypervisor));
    free(hypervisor);
}

static Hypervisor* setup_hypervisor(int argc, char* argv[])
{
    Hypervisor* hypervisor = calloc(1, sizeof(Hypervisor));

    if (hypervisor == NULL)
    {
        fprintf(stderr, "Error: Hypervisor memory allocation failed.\n");
        return NULL;
    }

    if (!setup_hypervisor_patterns(&hypervisor->patterns))
    {
        teardown_hypervisor(hypervisor);
        return NULL;
    }

    hypervisor->current_node = NULL;

    int opt = 0;

    /* Flawfinder: ignore - args are validated */
    while ((opt = getopt(argc, argv, "v:s:d")) != -1)
    {
        switch (opt)
        {
        case 'd':
            debug_enabled = true;
            debug("Debug enabled.\n");
            break;
        case 's':
            debug("NodeConfig->snapshot_file = \"%s\"\n", optarg);

            if (hypervisor->current_node == NULL)
            {
                fprintf(stderr, "Error: -s flag specified without preceding -v node.\n");
                teardown_hypervisor(hypervisor);
                return NULL;
            }

            if (hypervisor->current_node->config.snapshot_file != NULL)
            {
                free(hypervisor->current_node->config.snapshot_file);
            }

            hypervisor->current_node->config.snapshot_file = strndup(optarg, MAX_LINE_LEN);

            if (hypervisor->current_node->config.snapshot_file == NULL)
            {
                fprintf(stderr, "Error: NodeConfig.snapshot_file memory allocation failed.\n");
                teardown_hypervisor(hypervisor);
                return NULL;
            }
            break;
        case 'v':
            {
                debug("Node = \"%s\"\n", optarg);
                Node* node = setup_node(optarg, &hypervisor->patterns);

                if (node == NULL)
                {
                    teardown_hypervisor(hypervisor);
                    return NULL;
                }

                if (hypervisor->head_node == NULL)
                    hypervisor->head_node = node;
                else
                    hypervisor->current_node->next = node;

                hypervisor->active_nodes += 1;
                hypervisor->current_node = node;
                break;
            }
        default:
            break;
        }
    }

    Node* current = hypervisor->head_node;

    while (current != NULL)
    {
        Node* next = current->next;

        if (current->config.snapshot_file != NULL)
        {
            hypervisor->current_node = current;

            if (!restore_node(hypervisor))
                drop_node(hypervisor, current);
        }

        current = next;
    }

    hypervisor->current_node = hypervisor->head_node;

    return hypervisor;
}

static bool validate_args(int argc, char* argv[])
{
    if (argc == 1)
    {
        fprintf(stderr, "Error: No argument provided, exiting.\n");
        return false;
    }

    for (int i = 1; i < argc; i++)
    {
        if (argv[i] != NULL && strnlen(argv[i], MAX_ARG_LEN + 1) > MAX_ARG_LEN)
        {
            fprintf(stderr, "Error: argument exceeds maximum allowed length of %d.\n", MAX_ARG_LEN);
            return false;
        }
    }

    return true;
}

int main(const int argc, char* argv[])
{
    if (!validate_args(argc, argv))
        return 1;

    Hypervisor* hypervisor = setup_hypervisor(argc, argv);

    if (hypervisor == NULL)
        return 1;

    if (!run_hypervisor(hypervisor))
    {
        teardown_hypervisor(hypervisor);
        return 1;
    }

    teardown_hypervisor(hypervisor);
    return 0;
}
