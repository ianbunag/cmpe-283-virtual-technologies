#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// @TODO integration test
typedef struct NodeConfig
{
    char* instructions_file;
    char* snapshot_file;
} NodeConfig;

// @TODO socket/buffer DS
// typedef struct
// {
//     FILE *socket;
//     char buffer[256];
// } VMInstructions;

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

static Hypervisor* setupState(int argc, char* argv[])
{
    int opt;
    static Hypervisor hypervisor = {};
    Node* current_node = NULL;

    while ((opt = getopt(argc, argv, "v:s:")) != -1)
    {
        switch (opt)
        {
        case 'v': {
            Node* node = malloc(sizeof(Node));
            node->config.instructions_file = strdup(optarg);
            node->config.snapshot_file = NULL;
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
        case 's':
            if (current_node != NULL)
            {
                current_node->config.snapshot_file = strdup(optarg);
            }
            break;
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

        if (current_node->config.instructions_file != NULL)
        {
            memset(current_node->config.instructions_file, 0, strlen(current_node->config.instructions_file));
            free(current_node->config.instructions_file);
            current_node->config.instructions_file = NULL;
        }

        if (current_node->config.snapshot_file != NULL)
        {
            memset(current_node->config.snapshot_file, 0, strlen(current_node->config.snapshot_file));
            free(current_node->config.snapshot_file);
            current_node->config.snapshot_file = NULL;
        }

        memset(current_node, 0, sizeof(Node));
        free(current_node);
        current_node = next;
    }

    hypervisor->head_node = NULL;
}

int main(const int argc, char* argv[])
{
    Hypervisor* hypervisor = setupState(argc, argv);

    teardownState(hypervisor);

    return 0;
}
