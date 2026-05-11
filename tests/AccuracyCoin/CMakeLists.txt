set(ACCURACYCOIN_ROM "tests/AccuracyCoin/AccuracyCoin.nes")
set(ACCURACYCOIN_SCRIPT "tests/AccuracyCoin/test.lua")

add_test(
	NAME cNES_accuracycoin_lua
	COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script ${ACCURACYCOIN_SCRIPT}
	--build-target cNES_headless
)

set_tests_properties(cNES_accuracycoin_lua PROPERTIES
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
	#TIMEOUT 20
	WILL_FAIL FALSE
)