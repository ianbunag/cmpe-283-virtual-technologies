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

static NodeContainer setupNode(char* assembly_file_name)
{
    NodeContainer container;
    container.state = SUCCESS;
    container.node = calloc(1, sizeof(Node));

    container.node->config.assembly_file_name = strdup(assembly_file_name);

    /* Flawfinder: ignore - file name supplied by user. */
    FILE *assembly_file = fopen(assembly_file_name, "r");

    if (assembly_file == NULL)
    {
        fprintf(stderr, "Error: configuration \"%s\" cannot be opened.\n", assembly_file_name);
        container.state = ERROR;
        return container;
    }

    container.node->config.vm_binary = strdup("@TODO");

    fclose(assembly_file);

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
        memset(node->config.vm_binary, 0, strnlen(node->config.vm_binary, 4096));
        free(node->config.vm_binary);
        node->config.vm_binary = NULL;
    }

    memset(node, 0, sizeof(Node));
    free(node);
}

static HypervisorContainer setupHypervisor(int argc, char* argv[])
{
    int opt;
    HypervisorContainer container;
    container.state = SUCCESS;
    container.hypervisor = calloc(1, sizeof(Hypervisor));

    Node* current_node = NULL;

    /* Flawfinder: ignore - args are validated */
    while ((opt = getopt(argc, argv, "v:s:")) != -1)
    {
        switch (opt)
        {
        case 'v': {
            NodeContainer nodeContainer = setupNode(optarg);

            if (nodeContainer.state == ERROR)
            {
                container.state = ERROR;
                return container;
            }

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
