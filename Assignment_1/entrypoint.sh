#!/bin/bash
set -e

USER="ubuntu"
USER_HOME="/home/$USER"
SSH_DIR="$USER_HOME/.ssh"
PRIV_KEY="$SSH_DIR/id_rsa"
PUB_KEY="$SSH_DIR/id_rsa.pub"
AUTHORIZED_KEYS="$SSH_DIR/authorized_keys"

# Ensure home and .ssh exist
mkdir -p "$SSH_DIR"
chown -R $USER:$USER "$USER_HOME"
chmod 700 "$SSH_DIR"

# Generate keypair if missing
if [ ! -f "$PRIV_KEY" ]; then
    echo "[entrypoint] Generating SSH keypair for $USER..."
    ssh-keygen -t rsa -b 4096 -f "$PRIV_KEY" -N "" -C "$USER@container"
    chown $USER:$USER "$PRIV_KEY" "$PUB_KEY"
    chmod 600 "$PRIV_KEY"
    chmod 644 "$PUB_KEY"
fi

# Ensure authorized_keys exists and contains pubkey
if [ ! -f "$AUTHORIZED_KEYS" ]; then
    cp "$PUB_KEY" "$AUTHORIZED_KEYS"
    chown $USER:$USER "$AUTHORIZED_KEYS"
    chmod 600 "$AUTHORIZED_KEYS"
fi

echo "[entrypoint] Container ready!"
echo "[entrypoint] Connect using:"
echo "ssh -i ./docker/home/$USER/.ssh/id_rsa -p 2222 $USER@localhost"

# Run sshd
exec /usr/sbin/sshd -D
