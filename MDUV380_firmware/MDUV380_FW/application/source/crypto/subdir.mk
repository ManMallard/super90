################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../application/source/crypto/aes.c \
../application/source/crypto/dmr_crypto.c \
../application/source/crypto/enc_indicator.c \
../application/source/crypto/kdf.c \
../application/source/crypto/key_storage.c \
../application/source/crypto/sha256.c 

OBJS += \
./application/source/crypto/aes.o \
./application/source/crypto/dmr_crypto.o \
./application/source/crypto/enc_indicator.o \
./application/source/crypto/kdf.o \
./application/source/crypto/key_storage.o \
./application/source/crypto/sha256.o 

C_DEPS += \
./application/source/crypto/aes.d \
./application/source/crypto/dmr_crypto.d \
./application/source/crypto/enc_indicator.d \
./application/source/crypto/kdf.d \
./application/source/crypto/key_storage.d \
./application/source/crypto/sha256.d 


# Each subdirectory must supply rules for building sources it contributes
application/source/crypto/%.o application/source/crypto/%.su application/source/crypto/%.cyclo: ../application/source/crypto/%.c application/source/crypto/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DUSE_HAL_DRIVER -DSTM32F405xx -DPLATFORM_MDUV380 -DNDEBUG -DDEBUG -c -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../application/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../USB_DEVICE/Target -I../Drivers/CMSIS/Include -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../USB_DEVICE/App -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../ -Os -ffunction-sections -fdata-sections -Wall -Wno-format -Wno-format-truncation -Wno-stringop-overflow -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-application-2f-source-2f-crypto

clean-application-2f-source-2f-crypto:
	-$(RM) ./application/source/crypto/aes.cyclo ./application/source/crypto/aes.d ./application/source/crypto/aes.o ./application/source/crypto/aes.su ./application/source/crypto/dmr_crypto.cyclo ./application/source/crypto/dmr_crypto.d ./application/source/crypto/dmr_crypto.o ./application/source/crypto/dmr_crypto.su ./application/source/crypto/enc_indicator.cyclo ./application/source/crypto/enc_indicator.d ./application/source/crypto/enc_indicator.o ./application/source/crypto/enc_indicator.su ./application/source/crypto/kdf.cyclo ./application/source/crypto/kdf.d ./application/source/crypto/kdf.o ./application/source/crypto/kdf.su ./application/source/crypto/key_storage.cyclo ./application/source/crypto/key_storage.d ./application/source/crypto/key_storage.o ./application/source/crypto/key_storage.su ./application/source/crypto/sha256.cyclo ./application/source/crypto/sha256.d ./application/source/crypto/sha256.o ./application/source/crypto/sha256.su

.PHONY: clean-application-2f-source-2f-crypto

