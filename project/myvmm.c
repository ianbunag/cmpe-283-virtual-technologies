#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define MAX_LINE_LEN 4096
#define MAX_ARG_LEN 4096

typedef struct NodeConfig
{
    char* assembly_file_name;
    char* vm_binary;
} NodeConfig;

typedef struct NodeSocket
{
    FILE *file;
    bool has_pending_instructions;
} NodeSocket;

typedef struct Node
{
    NodeConfig config;
    NodeSocket socket;
    struct Node* next;
} Node;

typedef struct Hypervisor
{
    Node* head_node;
} Hypervisor;

typedef struct SplitStringContainer
{
    char* original;
    size_t original_length;
    size_t capacity;
    size_t count;
    char** strings;
} SplitStringContainer;

static bool validateArgs(int argc, char* argv[])
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
    container->strings = calloc(container->capacity, sizeof(char *));

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

    char *saveptr = NULL;
    char *string = strtok_r(container->original, delimiter, &saveptr);

    while (string != NULL) {
        if (container->count >= container->capacity) {
            container->capacity *= 2;
            char **reallocated_strings = realloc(container->strings, container->capacity * sizeof(char *));

            if (reallocated_strings == NULL) {
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

static void teardownNodeConfig(NodeConfig* config)
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

static bool setupNodeConfig(char* assembly_file_name, NodeConfig* config)
{
    memset(config, 0, sizeof(NodeConfig));
    config->assembly_file_name = strndup(assembly_file_name, MAX_LINE_LEN);

    if (config->assembly_file_name == NULL)
    {
        fprintf(stderr, "Error: NodeConfig.assembly_file_name memory allocation failed.\n");
        return false;
    }

    /* Flawfinder: ignore - file name supplied by user. */
    FILE *assembly_file = fopen(assembly_file_name, "r");

    if (assembly_file == NULL)
    {
        fprintf(stderr, "Error: configuration \"%s\" cannot be opened.\n", assembly_file_name);
        teardownNodeConfig(config);
        return false;
    }

    char *line = NULL;
    size_t len = 0;
    bool has_error = false;

    while (getline(&line, &len, assembly_file) != -1) {
        SplitStringContainer* strings = split_string(line, "=\r\n");

        if (strings == NULL)
        {
            has_error = true;
            break;
        }

        if (strings->count == 2)
        {
            if (strcmp(strings->strings[0], "vm_binary") == 0)
            {
                if (config->vm_binary != NULL)
                    free(config->vm_binary);

                config->vm_binary = strndup(strings->strings[1], MAX_LINE_LEN);

                if (config->vm_binary == NULL)
                {
                    fprintf(stderr, "Error: NodeConfig.vm_binary memory allocation failed.\n");
                    has_error = true;
                }
            }
        }

        teardown_split_string_container(strings);

        if (has_error)
        {
            break;
        }
    }

    fclose(assembly_file);
    free(line);

    if (has_error)
    {
        teardownNodeConfig(config);
        return false;
    }

    return true;
}

static void teardownNodeSocket(NodeSocket* socket)
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

static bool setupNodeSocket(char* vm_binary, NodeSocket* socket)
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

static void teardownNode(Node* node)
{
    if (node == NULL)
        return;

    teardownNodeConfig(&node->config);
    teardownNodeSocket(&node->socket);

    memset(node, 0, sizeof(Node));
    free(node);
}

static Node* setupNode(char* assembly_file_name)
{
    Node* node = calloc(1, sizeof(Node));

    if (node == NULL)
    {
        fprintf(stderr, "Error: Node memory allocation failed.\n");
        return NULL;
    }

    if (!setupNodeConfig(assembly_file_name, &node->config))
    {
        teardownNode(node);
        return NULL;
    }

    if (!setupNodeSocket(node->config.vm_binary, &node->socket))
    {
        teardownNode(node);
        return NULL;
    }

    return node;
}

static void teardownHypervisor(Hypervisor* hypervisor)
{
    if (hypervisor == NULL)
        return;

    Node* current_node = hypervisor->head_node;
    hypervisor->head_node = NULL;

    while (current_node != NULL)
    {
        Node* next = current_node->next;
        teardownNode(current_node);
        current_node = next;
    }

    memset(hypervisor, 0, sizeof(Hypervisor));
    free(hypervisor);
}

static Hypervisor* setupHypervisor(int argc, char* argv[])
{
    Hypervisor* hypervisor = calloc(1, sizeof(Hypervisor));
    if (hypervisor == NULL)
    {
        fprintf(stderr, "Error: Hypervisor memory allocation failed.\n");
        return NULL;
    }

    Node* current_node = NULL;
    int opt;

    /* Flawfinder: ignore - args are validated */
    while ((opt = getopt(argc, argv, "v:s:")) != -1)
    {
        switch (opt)
        {
        case 'v': {
            Node* node = setupNode(optarg);

            if (node == NULL)
            {
                teardownHypervisor(hypervisor);
                return NULL;
            }

            if (hypervisor->head_node == NULL)
                hypervisor->head_node = node;
            else
                current_node->next = node;

            current_node = node;
            break;
        }
        default:
            break;
        }
    }

    return hypervisor;
}

int main(const int argc, char* argv[])
{
    if (!validateArgs(argc, argv))
        return 1;

    Hypervisor* hypervisor = setupHypervisor(argc, argv);

    if (hypervisor == NULL)
        return 1;

    teardownHypervisor(hypervisor);

    return 0;
}
