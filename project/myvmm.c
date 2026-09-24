#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>

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

// https://stackoverflow.com/a/1726321
static void remove_spaces(char* s)
{
    char* d = s;

    do
    {
        while (isspace((unsigned char)*d))
        {
            ++d;
        }
    }
    while (*s++ = *d++);
}

typedef struct SplitStringContainer
{
    char* original;
    size_t original_length;
    size_t capacity;
    size_t count;
    char** strings;
} SplitStringContainer;

static void teardown_split_string_container(SplitStringContainer* container)
{
    if (container == NULL)
        return;

    if (container->original != NULL)
    {
        memset(container->original, 0, container->original_length);
        free(container->original);
        container->original = NULL;
    }

    if (container->strings != NULL)
    {
        // Strings are automatically zeroed out when container->original was cleaned up.
        free(container->strings);
        container->strings = NULL;
    }

    memset(container, 0, sizeof(SplitStringContainer));
    free(container);
}

static SplitStringContainer* split_string(char* input, const char* delimiter)
{
    SplitStringContainer* container = calloc(1, sizeof(SplitStringContainer));

    if (container == NULL)
    {
        fprintf(stderr, "Error: SplitStringContainer memory allocation failed.\n");
        return NULL;
    }

    container->original = strndup(input, MAX_LINE_LEN);
    container->original_length = strnlen(input, MAX_LINE_LEN);
    container->capacity = 1;
    container->count = 0;
    container->strings = calloc(container->capacity, sizeof(char*));

    if (container->original == NULL)
    {
        fprintf(stderr, "Error: SplitStringContainer.original string duplication failed.\n");
        teardown_split_string_container(container);
        return NULL;
    }

    if (container->strings == NULL)
    {
        fprintf(stderr, "Error: SplitStringContainer.strings memory allocation failed.\n");
        teardown_split_string_container(container);
        return NULL;
    }

    char* saveptr = NULL;
    char* string = strtok_r(container->original, delimiter, &saveptr);

    while (string != NULL)
    {
        if (container->count >= container->capacity)
        {
            container->capacity *= 2;
            char** reallocated_strings = realloc(container->strings, container->capacity * sizeof(char*));

            if (reallocated_strings == NULL)
            {
                fprintf(stderr, "Error: SplitStringContainer.strings memory reallocation failed.\n");
                teardown_split_string_container(container);
                return NULL;
            }

            container->strings = reallocated_strings;
        }

        container->strings[container->count++] = string;
        string = strtok_r(NULL, delimiter, &saveptr);
    }

    return container;
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
    unsigned int active_nodes;
} Hypervisor;

static bool validate_args(int argc, char* argv[])
{
    if (argc == 1)
    {
        fprintf(stdout, "No argument provided, exiting.\n");
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

static bool setup_node_config(char* assembly_file_name, NodeConfig* config)
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

    while (getline(&line, &len, assembly_file) != -1)
    {
        remove_spaces(line);
        SplitStringContainer* lines = split_string(line, "=");

        if (lines == NULL)
        {
            has_error = true;
            break;
        }

        if (lines->count == 2)
        {
            if (strcmp(lines->strings[0], "vm_binary") == 0)
            {
                if (config->vm_binary != NULL)
                    free(config->vm_binary);

                config->vm_binary = strndup(lines->strings[1], MAX_LINE_LEN);
                debug("NodeConfig->vm_binary = \"%s\"\n", config->vm_binary);

                if (config->vm_binary == NULL)
                {
                    fprintf(stderr, "Error: NodeConfig.vm_binary memory allocation failed.\n");
                    has_error = true;
                }
            }
        }

        teardown_split_string_container(lines);

        if (has_error)
        {
            break;
        }
    }

    fclose(assembly_file);
    if (line != NULL)
        free(line);

    if (has_error)
    {
        teardown_node_config(config);
        return false;
    }

    return true;
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

static Node* setup_node(char* assembly_file_name)
{
    Node* node = calloc(1, sizeof(Node));

    if (node == NULL)
    {
        fprintf(stderr, "Error: Node memory allocation failed.\n");
        return NULL;
    }

    if (!setup_node_config(assembly_file_name, &node->config))
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

static void teardown_hypervisor(Hypervisor* hypervisor)
{
    if (hypervisor == NULL)
        return;

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
                Node* node = setup_node(optarg);

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

static void switch_node(Hypervisor* hypervisor)
{
    hypervisor->current_node = (hypervisor->current_node->next != NULL)
                                   ? hypervisor->current_node->next
                                   : hypervisor->head_node;

    debug("Hypervisor->current_node = \"%s\"\n", hypervisor->current_node->config.vm_binary);
}

static void dump_processor_state(Node* node)
{
    for (int i = 0; i < 32; i++)
    {
        fprintf(stdout, "R%d=%d\n", i, node->processor.registers[i]);
    }
}


// @TODO normalize spaces
static bool get_register_key(const char* raw_key, int8_t* key)
{
    int numeric_part;

    if (sscanf(raw_key, "$%d", &numeric_part) != 1 || numeric_part < 0 || numeric_part > 31)
    {
        // @TODO test this case
        fprintf(stderr, "Error: Could not parse register key \"%s\"\n", raw_key);

        return false;
    }


    *key = (int8_t)numeric_part;

    return true;
}

static void set_register(NodeProcessor* processor, const char* raw_key, const char* raw_value)
{
    int8_t key;

    if (!get_register_key(raw_key, &key))
    {
        return;
    }

    int32_t value;

    if (sscanf(raw_value, "%d", &value) != 1)
    {
        // @TODO test this case
        fprintf(stderr, "Error: Could not parse register value \"%s\"\n", raw_value);
        return;
    }

    processor->registers[key] = value;
    debug("NodeProcessor->registers[%d] = %d\n", key, value);
}

// @TODO check in consumers if value was updated
static void get_register(const NodeProcessor* processor, const char* raw_key, int32_t* value)
{
    int8_t key;

    if (!get_register_key(raw_key, &key))
    {
        return;
    }

    *value = processor->registers[key];
}

static bool load_immediate(Node* node, char* raw_parameters)
{
    SplitStringContainer* parameters = split_string(raw_parameters, ",");

    if (parameters == NULL)
    {
        return false;
    }

    if (parameters->count == 2)
    {
        set_register(&node->processor, parameters->strings[0], parameters->strings[1]);
    }

    teardown_split_string_container(parameters);

    return true;
}

static bool run_node(Hypervisor* hypervisor)
{
    ssize_t chars_read;
    char* line = NULL;
    size_t len = 0;
    bool has_error = false;

    while ((chars_read = getline(&line, &len, hypervisor->current_node->socket.file)) != -1)
    {
        line[strcspn(line, "\r\n")] = '\0'; // Remove line breaks.
        debug("Running instruction \"%s\"\n", line);

        SplitStringContainer* instructions = split_string(line, " ");

        if (instructions == NULL)
        {
            has_error = true;
            break;
        }

        if (instructions->count == 1 && strcmp(instructions->strings[0], "DUMP_PROCESSOR_STATE") == 0)
        {
            dump_processor_state(hypervisor->current_node);
        }
        else if (instructions->count == 2)
        {
            if (strcmp(instructions->strings[0], "li") == 0)
            {
                has_error = !load_immediate(hypervisor->current_node, instructions->strings[1]);
            }
        }

        teardown_split_string_container(instructions);
        hypervisor->current_node->processor.program_counter += 1;

        if (has_error)
            break;
    }

    if (chars_read == -1)
        hypervisor->current_node->socket.has_pending_instructions = false;

    if (line != NULL)
        free(line);

    return has_error;
}

static void run_hypervisor(Hypervisor* hypervisor)
{
    if (hypervisor == NULL || hypervisor->head_node == NULL)
        return;

    switch_node(hypervisor);

    while (hypervisor->active_nodes > 0)
    {
        if (!hypervisor->current_node->socket.has_pending_instructions)
        {
            switch_node(hypervisor);
            continue;
        }

        if (run_node(hypervisor))
            break;

        if (!hypervisor->current_node->socket.has_pending_instructions)
            hypervisor->active_nodes -= 1;

        switch_node(hypervisor);
    }
}

int main(const int argc, char* argv[])
{
    if (!validate_args(argc, argv))
        return 1;

    Hypervisor* hypervisor = setup_hypervisor(argc, argv);

    if (hypervisor == NULL)
        return 1;

    run_hypervisor(hypervisor);
    teardown_hypervisor(hypervisor);

    return 0;
}
