FROM archlinux:latest

# Update the package repositories and install sudo
RUN pacman -Syu --noconfirm --needed sudo git base-devel

# Create a new user
RUN useradd -m user && \
  passwd -d user && \
  printf 'user ALL=(ALL) ALL\n' | tee -a /etc/sudoers

# Switch to the new user so makepkg doesn't run as root
USER user
WORKDIR /home/user

# Install paru package manager
RUN git clone https://aur.archlinux.org/paru.git && \
  cd paru && \
  makepkg -si --noconfirm

WORKDIR /app

USER root

# Define the command to run your application
CMD ["/bin/bash"]