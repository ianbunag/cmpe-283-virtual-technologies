#!/bin/bash
set -euo pipefail

if ! command -v ansible >/dev/null 2>&1; then
    apt-get update
    apt-get install -y pipx
    pipx ensurepath
    pipx install --include-deps ansible
fi

SSH_USER_HOME=$(eval echo "~${SSH_USERNAME}")
SSH_USER_DIR="$SSH_USER_HOME/.ssh"
SSH_USER_KEY_PATH="$SSH_USER_DIR/id_ed25519"

if [ ! -d "$SSH_USER_DIR" ]; then
    install -d -m 700 -o "${SSH_USERNAME}" -g "${SSH_USERNAME}" "$SSH_USER_DIR"
fi

if [ ! -f "$SSH_USER_KEY_PATH" ]; then
    sudo -u "${SSH_USERNAME}" -H bash -c 'umask 077; gcloud secrets versions access latest --secret="${DEPLOY_KEY_SECRET_NAME}" > "'"$SSH_USER_KEY_PATH"'"'
    chmod 600 "$SSH_USER_KEY_PATH"
fi

# Pre-populate GitHub's host keys so non-interactive git/ssh commands don't block on authenticity prompts.
if [ ! -f "$SSH_USER_DIR/known_hosts" ] || ! grep -q "github.com" "$SSH_USER_DIR/known_hosts"; then
    sudo -u "${SSH_USERNAME}" -H bash -c 'ssh-keyscan -t rsa,ecdsa,ed25519 github.com >> "'"$SSH_USER_DIR"'/known_hosts"' 2>/dev/null
    chmod 644 "$SSH_USER_DIR/known_hosts"
fi

export ANSIBLE_LOG_PATH=/tmp/ansible.log
export GIT_SSH_COMMAND="ssh -i $SSH_USER_KEY_PATH -o UserKnownHostsFile=$SSH_USER_DIR/known_hosts"

ansible-pull -U "${GIT_REPOSITORY}" ansible-playbook.yml
ansible-pull -U "${GIT_REPOSITORY}" ansible-ssh-user.yml
