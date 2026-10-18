# MIPS Virtual Machine Monitor (VMM)

A user-space virtual machine monitor and hypervisor written in C, featuring MIPS instruction emulation, round-robin multi-VM scheduling, state snapshotting, and TCP-based live migration across host systems.

> **Context:** Graduate Research Project — **CMPE-283: Virtual Technologies** (Fall 2026)  
> **Researcher:** [John Ian Buñag](https://github.com/ianbunag)  
> **Core Engine:** [myvmm.c](./project/myvmm.c)  
> **Infrastructure & Tooling:** [Terraform Cloud Lab](./terraform/) | [Ansible Automation](./ansible/)

## Prerequisites

### 1. Generate Deploy Key

Generate a dedicated ED25519 SSH key pair for the VM deploy key:

```bash
ssh-keygen -t ed25519 -C "cmpe-283.ianbunag.dev" -f ./id_ed25519_cmpe-283-ianbunag-dev
```

### 2. Add Deploy Key to GitHub Repository

Add the generated public key (`./id_ed25519_cmpe-283-ianbunag-dev.pub`) to the GitHub repository's deploy keys:

- Refer to the [GitHub Deploy Keys Documentation](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/managing-deploy-keys#set-up-deploy-keys).
- **Title**: Value of `deploy_key_secret_name` in [terraform/locals.tf](terraform/locals.tf)
- **Key**: Contents of `./id_ed25519_cmpe-283-ianbunag-dev.pub`

### 3. Create GCP Secret Manager Secret

Store the private key in Google Cloud Secret Manager:

- **Name**: Value of `deploy_key_secret_name` in [terraform/locals.tf](terraform/locals.tf)
- **Secret value**: Contents of `./id_ed25519_cmpe-283-ianbunag-dev` (the private key)
- **Replication policy**:
  - **Manually manage locations for this secret**: Enabled
  - **Location(s)**: `us-west1`

## Terraform Lab Provisioning

This repository's [`terraform/`](./terraform/) directory is designed as a reusable module to provision the virtualized research environment on Google Cloud Platform with Cloudflare DNS management:

```hcl
provider "cloudflare" {
  api_token = ""
}

provider "google" {
  project               = ""
  region                = "us-west1"
  zone                  = "us-west1-b"
  user_project_override = true
}

module "cmpe_283" {
  source = "git@github.com:ianbunag/mips-virtual-machine-monitor.git//terraform"
  root_domain                             = ""
  cloudflare_update_dns_token_secret_name = ""
  cloudflare_zone_id                      = ""
  ssh_public_key_file                     = ""
  ssh_username                            = ""
}
```

## Remote Development

### CLion Deployment Configuration

You can configure CLion for remote development via SFTP file synchronization using JetBrains' built-in deployment tooling. For general reference, see the [CLion Deployment Settings Documentation](https://www.jetbrains.com/help/clion/settings-deployment.html).

1. In CLion, navigate to **Tools** > **Deployment** > **Configuration...**.
2. Click **+** to add a new server configuration.

#### Connection Settings

| Setting | Value |
| :--- | :--- |
| **Type** | `SFTP` |
| **Root path** | `/home/<username>/Documents/remote-cmpe-283-virtual-technologies` |
| **Use rsync for download/upload/sync** | Enabled |

#### Mappings

Switch to the **Mappings** tab:

| Setting | Value |
| :--- | :--- |
| **Local path** | `<project-root>` |
| **Deployment path** | `/` |

### Enabling IntelliSense

To activate code completion and IntelliSense in CLion:

1. Open the source file containing the `main` function.
2. Click the green **Run** (play) button in the editor gutter beside `main`.

This allows CLion to resolve the execution context and index project symbols, enabling full code navigation, diagnostics, and auto-completion.
