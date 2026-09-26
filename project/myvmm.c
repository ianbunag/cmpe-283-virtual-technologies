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
    Pattern load_immediate;
    Pattern add;
    Pattern sub;
    Pattern add_immediate;
    Pattern add_unsigned;
    Pattern sub_unsigned;
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
} NodeConfig;

typedef struct NodeSocket
{
    FILE* file;
    bool has_pending_instructions;
} NodeSocket;

typedef struct NodeProcessor
{
    int32_t registers[32];
    int32_t program_counter;
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

    processor->registers[key] = value;
    debug("NodeProcessor->registers[%d] = %d\n", key, processor->registers[key]);
}

static void dump_processor_state(const NodeProcessor* processor)
{
    for (int i = 0; i < 32; i++)
    {
        fprintf(stdout, "R%d=%d\n", i, processor->registers[i]);
    }
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

static void sub(NodeProcessor* processor, const Match* match)
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

static void sub_unsigned(NodeProcessor* processor, const Match* match)
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
        debug("Running instruction \"%s\"\n", line);

        if (!initialize_match(&match))
        {
            has_error = true;
            break;
        }

        if (match_pattern(line, &hypervisor->patterns.dump_processor_state, match))
        {
            dump_processor_state(&hypervisor->current_node->processor);
        }
        else if (match_pattern(line, &hypervisor->patterns.dump_register_state, match))
        {
            dump_register_state(&hypervisor->current_node->processor, match);
        }
        else if (match_pattern(line, &hypervisor->patterns.load_immediate, match))
        {
            load_immediate(hypervisor->current_node, match);
        }
        else if (match_pattern(line, &hypervisor->patterns.add, match))
        {
            add(&hypervisor->current_node->processor, match);
        }
        else if (match_pattern(line, &hypervisor->patterns.sub, match))
        {
            sub(&hypervisor->current_node->processor, match);
        }
        else if (match_pattern(line, &hypervisor->patterns.add_immediate, match))
        {
            add_immediate(&hypervisor->current_node->processor, match);
        }
        else if (match_pattern(line, &hypervisor->patterns.add_unsigned, match))
        {
            add_unsigned(&hypervisor->current_node->processor, match);
        }
        else if (match_pattern(line, &hypervisor->patterns.sub_unsigned, match))
        {
            sub_unsigned(&hypervisor->current_node->processor, match);
        }
        else
        {
            fprintf(stderr, "Exception: Unknown instruction \"%s\"\n", line);
        }

        hypervisor->current_node->processor.program_counter += 1;
    }

    if (chars_read == -1)
        hypervisor->current_node->socket.has_pending_instructions = false;

    teardown_match(match);

    if (line != NULL)
        free(line);

    return !has_error;
}

static void switch_node(Hypervisor* hypervisor)
{
    hypervisor->current_node = (hypervisor->current_node->next != NULL)
                                   ? hypervisor->current_node->next
                                   : hypervisor->head_node;

    debug("Hypervisor->current_node = \"%s\"\n", hypervisor->current_node->config.vm_binary);
}

static bool run_hypervisor(Hypervisor* hypervisor)
{
    if (hypervisor == NULL || hypervisor->head_node == NULL)
        return false;

    switch_node(hypervisor);

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

    for (int i = 0; i < 32; i++)
    {
        processor->registers[i] = 0;
    }

    processor->program_counter = 0;
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

    if (patterns->load_immediate.is_initialized)
    {
        regfree(&patterns->load_immediate.pattern);
        patterns->load_immediate.groups = 0;
        patterns->load_immediate.is_initialized = false;
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

    if (patterns->add_immediate.is_initialized)
    {
        regfree(&patterns->add_immediate.pattern);
        patterns->add_immediate.groups = 0;
        patterns->add_immediate.is_initialized = false;
    }

    if (patterns->add_unsigned.is_initialized)
    {
        regfree(&patterns->add_unsigned.pattern);
        patterns->add_unsigned.groups = 0;
        patterns->add_unsigned.is_initialized = false;
    }

    if (patterns->sub_unsigned.is_initialized)
    {
        regfree(&patterns->sub_unsigned.pattern);
        patterns->sub_unsigned.groups = 0;
        patterns->sub_unsigned.is_initialized = false;
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
                "^[[:space:]]*DUMP_REGISTER_STATE[[:space:]]+\\$([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("dump_register_state", patterns);
    patterns->dump_register_state.groups = 1;
    patterns->dump_register_state.is_initialized = true;

    if (regcomp(&patterns->load_immediate.pattern,
                "^[[:space:]]*li[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*([-+]{0,1}[0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("load_immediate", patterns);
    patterns->load_immediate.groups = 2;
    patterns->load_immediate.is_initialized = true;

    if (regcomp(&patterns->add.pattern,
                "^[[:space:]]*add[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("add", patterns);
    patterns->add.groups = 3;
    patterns->add.is_initialized = true;

    if (regcomp(&patterns->sub.pattern,
                "^[[:space:]]*sub[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("sub", patterns);
    patterns->sub.groups = 3;
    patterns->sub.is_initialized = true;

    if (regcomp(&patterns->add_immediate.pattern,
                "^[[:space:]]*addi[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*,[[:space:]]*([-+]{0,1}[0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("add_immediate", patterns);
    patterns->add_immediate.groups = 3;
    patterns->add_immediate.is_initialized = true;

    if (regcomp(&patterns->add_unsigned.pattern,
                "^[[:space:]]*addu[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("add_unsigned", patterns);
    patterns->add_unsigned.groups = 3;
    patterns->add_unsigned.is_initialized = true;

    if (regcomp(&patterns->sub_unsigned.pattern,
                "^[[:space:]]*subu[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
        return fail_setup_hypervisor_pattern("sub_unsigned", patterns);
    patterns->sub_unsigned.groups = 3;
    patterns->sub_unsigned.is_initialized = true;

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
    int opt;

    /* Flawfinder: ignore - args are validated */
    while ((opt = getopt(argc, argv, "v:d")) != -1)
    {
        switch (opt)
        {
        case 'd':
            debug_enabled = true;
            debug("Debug enabled.\n");
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
