# Use Alpine Linux as the base image
FROM debian:latest

# Update package lists and install necessary dependencies
RUN apt-get update && apt-get -y install \
    git cmake libssl-dev build-essential

# Set the working directory inside the container
WORKDIR /MicroOcppSimulator

# Copy your application files to the container's working directory
COPY . .

RUN git submodule init && git submodule update
RUN cmake -S . -B ./build
RUN cmake --build ./build -j 16 --target mo_simulator -j 16

# Grant execute permissions to the shell script
RUN chmod +x /MicroOcppSimulator/build/mo_simulator

# Expose port 8000
EXPOSE 8000

# Run the shell script inside the container
CMD ["./build/mo_simulator"]
