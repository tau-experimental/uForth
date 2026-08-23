################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../forth_cell_pool.c \
../forth_chunk_pool.c \
../forth_debug.c \
../forth_dict.c \
../forth_heap.c \
../forth_vm.c \
../hardware.c \
../main.c 

C_DEPS += \
./forth_cell_pool.d \
./forth_chunk_pool.d \
./forth_debug.d \
./forth_dict.d \
./forth_heap.d \
./forth_vm.d \
./hardware.d \
./main.d 

OBJS += \
./forth_cell_pool.o \
./forth_chunk_pool.o \
./forth_debug.o \
./forth_dict.o \
./forth_heap.o \
./forth_vm.o \
./hardware.o \
./main.o 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean--2e-

clean--2e-:
	-$(RM) ./forth_cell_pool.d ./forth_cell_pool.o ./forth_chunk_pool.d ./forth_chunk_pool.o ./forth_debug.d ./forth_debug.o ./forth_dict.d ./forth_dict.o ./forth_heap.d ./forth_heap.o ./forth_vm.d ./forth_vm.o ./hardware.d ./hardware.o ./main.d ./main.o

.PHONY: clean--2e-

