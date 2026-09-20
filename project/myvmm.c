#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct NodeConfig
{
    char* vm_binary;
    int vm_exec_slice_in_instructions;
} NodeConfig;

typedef struct
{
    FILE *file;
    char buffer[64];
    char has_pending_instructions;
} Socket;

typedef struct Node
{
    NodeConfig config;
    struct Node* next;
    // VMInstructions vm_instructions;
} Node;

typedef struct Hypervisor
{
    Node* head_node;
} Hypervisor;

#define MAX_ARG_LEN 4096

static int validateArgs(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (argv[i] != NULL && strnlen(argv[i], MAX_ARG_LEN + 1) > MAX_ARG_LEN)
        {
            fprintf(stderr, "Error: argument exceeds maximum allowed length of %d.\n", MAX_ARG_LEN);
            return 0;
        }
    }

    return 1;
}

static Hypervisor* setupState(int argc, char* argv[])
{
    int opt;
    static Hypervisor hypervisor = {};
    Node* current_node = NULL;

    /* Flawfinder: ignore - args are validated */
    while ((opt = getopt(argc, argv, "v:s:")) != -1)
    {
        switch (opt)
        {
        case 'v': {
            Node* node = malloc(sizeof(Node));
            node->config.vm_binary = strdup(optarg);
            node->next = NULL;

            if (hypervisor.head_node == NULL)
            {
                hypervisor.head_node = node;
                current_node = node;
            }
            else
            {
                current_node->next = node;
                current_node = node;
            }
            break;
        }
        default:
            break;
        }
    }

    return &hypervisor;
}

static void teardownState(Hypervisor* hypervisor)
{
    if (hypervisor == NULL || hypervisor->head_node == NULL)
    {
        return;
    }

    Node* current_node = hypervisor->head_node;

    while (current_node != NULL)
    {
        Node* next = current_node->next;

        if (current_node->config.vm_binary != NULL)
        {
            memset(current_node->config.vm_binary, 0, strnlen(current_node->config.vm_binary, 4096));
            free(current_node->config.vm_binary);
            current_node->config.vm_binary = NULL;
        }

        memset(current_node, 0, sizeof(Node));
        free(current_node);
        current_node = next;
    }

    hypervisor->head_node = NULL;
}

int main(const int argc, char* argv[])
{
    if (validateArgs(argc, argv))
    {
        return 1;
    }

    Hypervisor* hypervisor = setupState(argc, argv);
    teardownState(hypervisor);

    return 0;
}
