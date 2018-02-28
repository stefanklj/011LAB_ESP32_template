deps_config := \
	/home/Kljakic/esp-idf/components/app_trace/Kconfig \
	/home/Kljakic/esp-idf/components/aws_iot/Kconfig \
	/home/Kljakic/esp-idf/components/bt/Kconfig \
	/home/Kljakic/esp-idf/components/esp32/Kconfig \
	/home/Kljakic/esp-idf/components/ethernet/Kconfig \
	/home/Kljakic/esp-idf/components/fatfs/Kconfig \
	/home/Kljakic/esp-idf/components/freertos/Kconfig \
	/home/Kljakic/esp-idf/components/heap/Kconfig \
	/home/Kljakic/esp-idf/components/libsodium/Kconfig \
	/home/Kljakic/esp-idf/components/log/Kconfig \
	/home/Kljakic/esp-idf/components/lwip/Kconfig \
	/home/Kljakic/esp-idf/components/mbedtls/Kconfig \
	/home/Kljakic/esp-idf/components/openssl/Kconfig \
	/home/Kljakic/esp-idf/components/pthread/Kconfig \
	/home/Kljakic/esp-idf/components/spi_flash/Kconfig \
	/home/Kljakic/esp-idf/components/spiffs/Kconfig \
	/home/Kljakic/esp-idf/components/tcpip_adapter/Kconfig \
	/home/Kljakic/esp-idf/components/wear_levelling/Kconfig \
	/home/Kljakic/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/Kljakic/esp-idf/components/esptool_py/Kconfig.projbuild \
	/home/Kljakic/07_gpio_interrupts/main/Kconfig.projbuild \
	/home/Kljakic/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/Kljakic/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
