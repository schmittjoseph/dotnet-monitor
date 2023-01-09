# Alpine Dev Container

The current Codespace is using an Alpine dev container, which is only recommend if need to test changes to `dotnet-monitor` on Alpine's musl libc. For most other workflows the [Debian dev container](../glibc/devcontainer.json) is recommend instead as it more tooling available.

## The following feature are **NOT** currently supported in Alpine dev containers
- docker-in-docker
- az cli
- nodejs
- github cli
- kubectl-helm-minikube
- powershell