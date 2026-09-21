#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct NodeConfig
{
    char* assembly_file_name;
    char* vm_binary;
} NodeConfig;

// typedef struct
// {
//     FILE *file;
//     char buffer[64];
//     char has_pending_instructions;
// } Socket;

typedef struct Node
{
    NodeConfig config;
    struct Node* next;
} Node;

typedef struct NodeContainer
{
    int state;
    Node* node;
} NodeContainer;

typedef struct Hypervisor
{
    Node* head_node;
} Hypervisor;

typedef struct HypervisorContainer
{
    int state;
    Hypervisor* hypervisor;
} HypervisorContainer;

typedef struct SplitStringContainer
{
    int state;
    char* original;
    size_t original_length;
    size_t capacity;
    size_t count;
    char** strings;
} SplitStringContainer;

#define MAX_STRN_LEN 4096
#define MAX_ARG_LEN 4096

const int SUCCESS = 0;
const int ERROR = 1;

static int validateArgs(int argc, char* argv[])
{
    if (argc == 1)
    {
        fprintf(stdout, "No argument provided, exiting.\n");
        return SUCCESS;
    }

    for (int i = 1; i < argc; i++)
    {
        if (argv[i] != NULL && strnlen(argv[i], MAX_ARG_LEN + 1) > MAX_ARG_LEN)
        {
            fprintf(stderr, "Error: argument exceeds maximum allowed length of %d.\n", MAX_ARG_LEN);
            return ERROR;
        }
    }

    return SUCCESS;
}

static SplitStringContainer split_string(char* input, const char* delimiter)
{
    SplitStringContainer container = {0};
    container.state = SUCCESS;
    container.original = strdup(input);
    container.original_length = strnlen(input, MAX_STRN_LEN);
    container.capacity = 1;
    container.count = 0;
    container.strings = calloc(container.capacity, sizeof(char *));

    if (container.original == NULL)
    {
        fprintf(stderr, "Error: SplitStringContainer.original string duplication failed.\n");
        container.state = ERROR;
        return container;
    }

    if (container.strings == NULL)
    {
        fprintf(stderr, "Error: SplitStringContainer.strings memory allocation failed.\n");
        container.state = ERROR;
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
                container.state = ERROR;
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

static NodeContainer setupNode(char* assembly_file_name)
{
    NodeContainer container = {0};
    container.state = SUCCESS;
    container.node = calloc(1, sizeof(Node));

    if (container.node == NULL)
    {
        fprintf(stderr, "Error: NodeContainer.node memory allocation failed.\n");
        container.state = ERROR;
        return container;
    }

    container.node->config.assembly_file_name = strdup(assembly_file_name);

    if (container.node->config.assembly_file_name == NULL)
    {
        fprintf(stderr, "Error: NodeConfig.assembly_file_name memory allocation failed.\n");
        container.state = ERROR;
        return container;
    }

    /* Flawfinder: ignore - file name supplied by user. */
    FILE *assembly_file = fopen(assembly_file_name, "r");

    if (assembly_file == NULL)
    {
        fprintf(stderr, "Error: configuration \"%s\" cannot be opened.\n", assembly_file_name);
        container.state = ERROR;
        return container;
    }

    char *line = NULL;
    size_t len = 0;

    while (getline(&line, &len, assembly_file) != -1) {
        SplitStringContainer strings = split_string(line, "=");

        if (strings.state == ERROR)
        {
            fclose(assembly_file);
            free(line);
            teardown_split_string_container(strings);
            container.state = ERROR;
            return container;
        }

        teardown_split_string_container(strings);
    }

    // @TODO validate
    container.node->config.vm_binary = strdup("@TODO");

    fclose(assembly_file);
    free(line);

    return container;
}

static void teardownNode(Node* node)
{
    if (node->config.assembly_file_name != NULL)
    {
        memset(node->config.assembly_file_name, 0, strnlen(node->config.assembly_file_name, MAX_ARG_LEN));
        free(node->config.assembly_file_name);
        node->config.assembly_file_name = NULL;
    }

    if (node->config.vm_binary != NULL)
    {
        memset(node->config.vm_binary, 0, strnlen(node->config.vm_binary, MAX_STRN_LEN));
        free(node->config.vm_binary);
        node->config.vm_binary = NULL;
    }

    memset(node, 0, sizeof(Node));
    free(node);
}

static HypervisorContainer setupHypervisor(int argc, char* argv[])
{
    HypervisorContainer container = {0};
    container.state = SUCCESS;
    container.hypervisor = calloc(1, sizeof(Hypervisor));
    int opt;

    if (container.hypervisor == NULL)
    {
        fprintf(stderr, "Error: HypervisorContainer.hypervisor memory allocation failed.\n");
        container.state = ERROR;
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
            if (nodeContainer.state == ERROR)
            {
                container.state = ERROR;
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
    if (validateArgs(argc, argv) == ERROR)
    {
        return ERROR;
    }

    HypervisorContainer container = setupHypervisor(argc, argv);

    if (container.state == ERROR)
    {
        teardownHypervisor(container.hypervisor);
        return ERROR;
    }

    teardownHypervisor(container.hypervisor);
    return SUCCESS;
}
