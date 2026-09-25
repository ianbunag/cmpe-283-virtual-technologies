#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdarg.h>
#include <regex.h>

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

typedef struct NodePatterns
{
    regex_t vm_binary;
} NodePatterns;

typedef struct InstructionPatterns
{
    regex_t dump_processor_state;
    regex_t dump_register_state;
    regex_t load_immediate;
    regex_t add;
    regex_t sub;
} InstructionPatterns;

typedef struct Patterns
{
    bool _is_vm_binary_initialized;
    bool _is_dump_processor_state_initialized;
    bool _is_dump_register_state_initialized;
    bool _is_load_immediate_initialized;
    bool _is_add_initialized;
    bool _is_sub_initialized;
    NodePatterns node;
    InstructionPatterns instruction;
} Patterns;

typedef struct MatchedPattern
{
    bool is_match;
    bool has_error;
    char** capture_groups;
} MatchedPattern;

static void teardown_matched_pattern(MatchedPattern* match)
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

    memset(match, 0, sizeof(MatchedPattern));
    free(match);
}

static bool match_pattern(const char* string, const regex_t* pattern, const size_t max_groups, MatchedPattern* match)
{
    if (match->has_error)
        return false;

    if (string == NULL || pattern == NULL || match == NULL)
        return false;

    regmatch_t matched_groups[max_groups + 1];

    if (regexec(pattern, string, max_groups + 1, matched_groups, 0) != 0)
        return false;

    match->is_match = true;

    if (max_groups > 0)
    {
        match->capture_groups = calloc(max_groups + 1, sizeof(char*));

        if (match->capture_groups == NULL)
        {
            fprintf(stderr, "Error: MatchedPattern->capture_groups memory allocation failed.\n");
            match->has_error = true;
            return false;
        }

        for (size_t i = 0; i < max_groups; i++)
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

static bool initialize_matched_pattern(MatchedPattern** pattern)
{
    if (pattern == NULL)
        return false;

    if (*pattern == NULL)
    {
        *pattern = calloc(1, sizeof(MatchedPattern));
        if (*pattern == NULL)
        {
            fprintf(stderr, "Error: MatchedPattern memory allocation failed.\n");
            return false;
        }
        (*pattern)->has_error = false;
        return true;
    }

    if ((*pattern)->has_error)
        return false;

    (*pattern)->is_match = false;

    if ((*pattern)->capture_groups != NULL)
    {
        for (size_t i = 0; (*pattern)->capture_groups[i] != NULL; i++)
        {
            free((*pattern)->capture_groups[i]);
            (*pattern)->capture_groups[i] = NULL;
        }
        free((*pattern)->capture_groups);
        (*pattern)->capture_groups = NULL;
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
        fprintf(stderr, "Error: Invalid register \"$%s\"\n", raw_key);

        return false;
    }

    *key = (int8_t)converted_key;

    return true;
}

static bool get_register(const NodeProcessor processor, const char* raw_key, int32_t* value)
{
    int8_t key;

    if (!get_register_key(raw_key, &key))
        return false;

    *value = processor.registers[key];
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

static void dump_processor_state(const NodeProcessor processor)
{
    for (int i = 0; i < 32; i++)
    {
        fprintf(stdout, "R%d=%d\n", i, processor.registers[i]);
    }
}

static void dump_register_state(const NodeProcessor processor, const char* raw_key)
{
    int32_t value;

    if (get_register(processor, raw_key, &value))
    {
        fprintf(stdout, "R%s=%d\n", raw_key, value);
    }
}

static void load_immediate(Node* node, const char* raw_key, const char* raw_value)
{
    set_register(&node->processor, raw_key, (int32_t)strtol(raw_value, NULL, 10));
}

static void add(NodeProcessor* processor, const char* raw_destination_key, const char* raw_source_key_1,
                const char* raw_source_key_2)
{
    int32_t source_value_1;
    int32_t source_value_2;

    if (!get_register(*processor, raw_source_key_1, &source_value_1) || !get_register(
        *processor, raw_source_key_2, &source_value_2))
    {
        return;
    }

    set_register(processor, raw_destination_key, source_value_1 + source_value_2);
}

static void sub(NodeProcessor* processor, const char* raw_destination_key, const char* raw_source_key_1,
                const char* raw_source_key_2)
{
    int32_t source_value_1;
    int32_t source_value_2;

    if (!get_register(*processor, raw_source_key_1, &source_value_1) || !get_register(
        *processor, raw_source_key_2, &source_value_2))
    {
        return;
    }

    set_register(processor, raw_destination_key, source_value_1 - source_value_2);
}

static bool run_node(Hypervisor* hypervisor)
{
    ssize_t chars_read;
    char* line = NULL;
    size_t len = 0;
    MatchedPattern* match = NULL;

    while ((chars_read = getline(&line, &len, hypervisor->current_node->socket.file)) != -1)
    {
        line[strcspn(line, "\r\n")] = '\0'; // Remove line breaks.
        debug("Running instruction \"%s\"\n", line);

        if (!initialize_matched_pattern(&match))
        {
            return false;
        }

        if (match_pattern(line, &hypervisor->patterns.instruction.dump_processor_state, 0, match))
        {
            dump_processor_state(hypervisor->current_node->processor);
        }
        else if (match_pattern(line, &hypervisor->patterns.instruction.dump_register_state, 1, match))
        {
            dump_register_state(hypervisor->current_node->processor, match->capture_groups[0]);
        }
        else if (match_pattern(line, &hypervisor->patterns.instruction.load_immediate, 2, match))
        {
            load_immediate(hypervisor->current_node, match->capture_groups[0], match->capture_groups[1]);
        }
        else if (match_pattern(line, &hypervisor->patterns.instruction.add, 3, match))
        {
            add(&hypervisor->current_node->processor, match->capture_groups[0], match->capture_groups[1],
                match->capture_groups[2]);
        }
        else if (match_pattern(line, &hypervisor->patterns.instruction.sub, 3, match))
        {
            sub(&hypervisor->current_node->processor, match->capture_groups[0], match->capture_groups[1],
                match->capture_groups[2]);
        }
        else
        {
            fprintf(stderr, "Unknown instruction \"%s\"\n", line);
        }

        hypervisor->current_node->processor.program_counter += 1;
    }

    if (chars_read == -1)
        hypervisor->current_node->socket.has_pending_instructions = false;

    teardown_matched_pattern(match);

    if (line != NULL)
        free(line);

    return true;
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
    MatchedPattern* match = NULL;

    while (getline(&line, &len, assembly_file) != -1)
    {
        if (!initialize_matched_pattern(&match))
        {
            has_error = true;
            break;
        }

        if (match_pattern(line, &patterns->node.vm_binary, 1, match))
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
    teardown_matched_pattern(match);

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

    if (patterns->_is_vm_binary_initialized)
    {
        regfree(&patterns->node.vm_binary);
        patterns->_is_vm_binary_initialized = false;
    }

    if (patterns->_is_dump_processor_state_initialized)
    {
        regfree(&patterns->instruction.dump_processor_state);
        patterns->_is_dump_processor_state_initialized = false;
    }

    if (patterns->_is_dump_register_state_initialized)
    {
        regfree(&patterns->instruction.dump_register_state);
        patterns->_is_dump_register_state_initialized = false;
    }

    if (patterns->_is_load_immediate_initialized)
    {
        regfree(&patterns->instruction.load_immediate);
        patterns->_is_load_immediate_initialized = false;
    }

    if (patterns->_is_add_initialized)
    {
        regfree(&patterns->instruction.add);
        patterns->_is_add_initialized = false;
    }

    if (patterns->_is_sub_initialized)
    {
        regfree(&patterns->instruction.sub);
        patterns->_is_sub_initialized = false;
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

    if (regcomp(&patterns->node.vm_binary,
                "^[[:space:]]*vm_binary[[:space:]]*=[[:space:]]*([^[:space:]]+)[[:space:]]*$", REG_EXTENDED) != 0)
    {
        fail_setup_hypervisor_pattern("vm_binary", patterns);
        return false;
    }
    patterns->_is_vm_binary_initialized = true;

    if (regcomp(&patterns->instruction.dump_processor_state, "^[[:space:]]*DUMP_PROCESSOR_STATE[[:space:]]*$",
                REG_EXTENDED) != 0)
    {
        fail_setup_hypervisor_pattern("dump_processor_state", patterns);
        return false;
    }
    patterns->_is_dump_processor_state_initialized = true;

    if (regcomp(&patterns->instruction.dump_register_state,
                "^[[:space:]]*DUMP_REGISTER_STATE[[:space:]]+\\$([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
    {
        fail_setup_hypervisor_pattern("dump_register_state", patterns);
        return false;
    }
    patterns->_is_dump_register_state_initialized = true;

    if (regcomp(&patterns->instruction.load_immediate,
                "^[[:space:]]*li[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*([-+]{0,1}[0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
    {
        fail_setup_hypervisor_pattern("load_immediate", patterns);
        return false;
    }
    patterns->_is_load_immediate_initialized = true;

    if (regcomp(&patterns->instruction.add,
                "^[[:space:]]*add[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
    {
        fail_setup_hypervisor_pattern("add", patterns);
        return false;
    }
    patterns->_is_add_initialized = true;

    if (regcomp(&patterns->instruction.sub,
                "^[[:space:]]*sub[[:space:]]+\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*,[[:space:]]*\\$([0-9]+)[[:space:]]*$",
                REG_EXTENDED) != 0)
    {
        fail_setup_hypervisor_pattern("sub", patterns);
        return false;
    }
    patterns->_is_sub_initialized = true;

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
        fprintf(stderr, "No argument provided, exiting.\n");
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
