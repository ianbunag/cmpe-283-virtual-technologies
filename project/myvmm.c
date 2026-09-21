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

typedef struct NodeConfigContainer
{
    bool has_error;
    NodeConfig config;
} NodeConfigContainer;

typedef struct NodeSocket
{
    FILE *file;
    bool has_pending_instructions;
} NodeSocket;

typedef struct NodeSocketContainer
{
    bool has_error;
    NodeSocket socket;
} NodeSocketContainer;

typedef struct Node
{
    NodeConfig config;
    NodeSocket socket;
    struct Node* next;
} Node;

typedef struct NodeContainer
{
    bool has_error;
    Node* node;
} NodeContainer;

typedef struct Hypervisor
{
    Node* head_node;
} Hypervisor;

typedef struct HypervisorContainer
{
    bool has_error;
    Hypervisor* hypervisor;
} HypervisorContainer;

typedef struct SplitStringContainer
{
    bool has_error;
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

static SplitStringContainer split_string(char* input, const char* delimiter)
{
    SplitStringContainer container = {0};
    container.original = strndup(input, MAX_LINE_LEN);
    container.original_length = strnlen(input, MAX_LINE_LEN);
    container.capacity = 1;
    container.count = 0;
    container.strings = calloc(container.capacity, sizeof(char *));

    if (container.original == NULL)
    {
        fprintf(stderr, "Error: SplitStringContainer.original string duplication failed.\n");
        container.has_error = true;
        return container;
    }

    if (container.strings == NULL)
    {
        fprintf(stderr, "Error: SplitStringContainer.strings memory allocation failed.\n");
        container.has_error = true;
        return container;
    }

    char *saveptr = NULL;
    char *string = strtok_r(container.original, delimiter, &saveptr);

    while (string != NULL) {
        if (container.count >= container.capacity) {
            container.capacity *= 2;
            char **reallocated_strings = realloc(container.strings, container.capacity * sizeof(char *));

            if (reallocated_strings == NULL) {
                fprintf(stderr, "Error: SplitStringContainer.strings memory reallocation failed.\n");
                container.has_error = true;
                return container;
            }

            container.strings = reallocated_strings;
        }

        container.strings[container.count++] = string;
        string = strtok_r(NULL, delimiter, &saveptr);
    }

    return container;
}

static void teardown_split_string_container(SplitStringContainer container)
{
    if (container.original != NULL)
    {
        memset(container.original, 0, container.original_length);
        free(container.original);
        container.original = NULL;
    }


    if (container.strings != NULL)
    {
        free(container.strings);
        container.strings = NULL;
    }
}

static NodeConfigContainer setupNodeConfig(char* assembly_file_name)
{
    NodeConfigContainer container = {0};
    container.config.assembly_file_name = strndup(assembly_file_name, MAX_LINE_LEN);

    if (container.config.assembly_file_name == NULL)
    {
        fprintf(stderr, "Error: NodeConfig.assembly_file_name memory allocation failed.\n");
        container.has_error = true;
        return container;
    }

    /* Flawfinder: ignore - file name supplied by user. */
    FILE *assembly_file = fopen(assembly_file_name, "r");

    if (assembly_file == NULL)
    {
        fprintf(stderr, "Error: configuration \"%s\" cannot be opened.\n", assembly_file_name);
        container.has_error = true;
        return container;
    }

    char *line = NULL;
    size_t len = 0;
    bool has_error = false;

    while (getline(&line, &len, assembly_file) != -1) {
        SplitStringContainer strings = split_string(line, "=\r\n");

        if (strings.has_error)
            has_error = true;
        else if (strings.count == 2)
        {
            if (strcmp(strings.strings[0], "vm_binary") == 0)
            {
                if (container.config.vm_binary != NULL)
                    free(container.config.vm_binary);
                container.config.vm_binary = strndup(strings.strings[1], MAX_LINE_LEN);

                if (container.config.vm_binary == NULL)
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

    container.has_error = has_error;
    fclose(assembly_file);
    free(line);

    return container;
}

static void teardownNodeConfig(NodeConfig config)
{
    if (config.assembly_file_name != NULL)
    {
        memset(config.assembly_file_name, 0, strnlen(config.assembly_file_name, MAX_ARG_LEN));
        free(config.assembly_file_name);
        config.assembly_file_name = NULL;
    }

    if (config.vm_binary != NULL)
    {
        memset(config.vm_binary, 0, strnlen(config.vm_binary, MAX_LINE_LEN));
        free(config.vm_binary);
        config.vm_binary = NULL;
    }
}

static NodeSocketContainer setupNodeSocket(char* vm_binary)
{
    NodeSocketContainer container = {0};
    /* Flawfinder: ignore - file name supplied by user. */
    container.socket.file = fopen(vm_binary, "r");

    if (container.socket.file == NULL)
    {
        fprintf(stderr, "Error: vm_binary \"%s\" cannot be opened.\n", vm_binary);
        container.has_error = true;
        return container;
    }

    return container;
}

static void teardownNodeSocket(NodeSocket socket)
{
    if (socket.file != NULL)
        fclose(socket.file);
}

static NodeContainer setupNode(char* assembly_file_name)
{
    NodeContainer container = {0};
    container.node = calloc(1, sizeof(Node));

    if (container.node == NULL)
    {
        fprintf(stderr, "Error: NodeContainer.node memory allocation failed.\n");
        container.has_error = true;
        return container;
    }

    NodeConfigContainer configContainer = setupNodeConfig(assembly_file_name);
    container.node->config = configContainer.config;

    if (configContainer.has_error)
    {
        container.has_error = true;
        return container;
    }

    NodeSocketContainer socketContainer = setupNodeSocket(container.node->config.vm_binary);
    container.node->socket = socketContainer.socket;

    if (socketContainer.has_error)
    {
        container.has_error = true;
        return container;
    }

    return container;
}

static void teardownNode(Node* node)
{
    if (node == NULL)
        return;

    teardownNodeConfig(node->config);
    teardownNodeSocket(node->socket);

    memset(node, 0, sizeof(Node));
    free(node);
}

static HypervisorContainer setupHypervisor(int argc, char* argv[])
{
    HypervisorContainer container = {0};
    container.hypervisor = calloc(1, sizeof(Hypervisor));
    int opt;

    if (container.hypervisor == NULL)
    {
        fprintf(stderr, "Error: HypervisorContainer.hypervisor memory allocation failed.\n");
        container.has_error = true;
        return container;
    }

    Node* current_node = NULL;

    /* Flawfinder: ignore - args are validated */
    while ((opt = getopt(argc, argv, "v:s:")) != -1)
    {
        switch (opt)
        {
        case 'v': {
            NodeContainer nodeContainer = setupNode(optarg);

            if (container.hypervisor->head_node == NULL)
            {
                container.hypervisor->head_node = nodeContainer.node;
                current_node = nodeContainer.node;
            }
            else
            {
                current_node->next = nodeContainer.node;
                current_node = nodeContainer.node;
            }

            // Allow node to be torn down with the hypervisor.
            if (nodeContainer.has_error)
            {
                container.has_error = true;
                return container;
            }
            break;
        }
        default:
            break;
        }
    }

    return container;
}

static void teardownHypervisor(Hypervisor* hypervisor)
{
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

int main(const int argc, char* argv[])
{
    if (!validateArgs(argc, argv))
        return 1;

    HypervisorContainer container = setupHypervisor(argc, argv);

    if (container.has_error)
    {
        teardownHypervisor(container.hypervisor);
        return 1;
    }

    teardownHypervisor(container.hypervisor);
    return 0;
}
