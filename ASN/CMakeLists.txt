#
# OpenBTS-UMTS/ASN/CMakeLists.txt
#

include(files.cmake) # -> asn_files_rrl

set(generated_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)

set(asn_files_common
	BOOLEAN.c
	BIT_STRING.c
	ENUMERATED.c
	INTEGER.c
	NULL.c
	NativeEnumerated.c
	NativeInteger.c
	OCTET_STRING.c
	asn_SEQUENCE_OF.c
	asn_SET_OF.c
	asn_codecs_prim.c
	ber_decoder.c
	ber_tlv_length.c
	ber_tlv_tag.c
	constr_CHOICE.c
	constr_SEQUENCE.c
	constr_SEQUENCE_OF.c
	constr_SET_OF.c
	constr_TYPE.c
	constraints.c
	der_encoder.c
	per_decoder.c
	per_encoder.c
	per_opentype.c
	per_support.c
	xer_decoder.c
	xer_encoder.c
	xer_support.c
	# converter-sample.c
	# pdu_collection.c
)

add_compile_options(-Werror)

set_source_files_properties(
	${generated_dir}/InterRATHandoverInfo.c
	${generated_dir}/RRCConnectionSetupComplete-v690ext-IEs.c
	${generated_dir}/RRCConnectionSetupComplete.c
	${generated_dir}/SRNC-RelocationInfo-r3.c
	${generated_dir}/SRNC-RelocationInfo-r6-IEs.c
	${generated_dir}/SRNC-RelocationInfo-v690ext-IEs.c
	${generated_dir}/UECapabilityInformation-v690ext-IEs.c
	${generated_dir}/UECapabilityInformation.c
	PROPERTIES COMPILE_FLAGS -Wno-unused-variable
)

set_source_files_properties(${generated_dir}/xer_decoder.c
	PROPERTIES COMPILE_FLAGS -Wno-maybe-uninitialized
)

foreach(filename ${asn_files_common})
	set(asn_files_common_o ${asn_files_common_o} ${generated_dir}/${filename})
	set_source_files_properties(${generated_dir}/${filename} PROPERTIES GENERATED TRUE)
endforeach()

foreach(filename ${asn_files_rrl})
	set(asn_files_rrl_o ${asn_files_rrl_o} ${generated_dir}/${filename})
	set_source_files_properties(${generated_dir}/${filename} PROPERTIES GENERATED TRUE)
endforeach()

file(MAKE_DIRECTORY ${generated_dir})

add_custom_command(
	OUTPUT ${asn_files_common_o} ${asn_files_rrl_o}
	COMMAND asn1c -fcompound-names -pdu=auto -gen-PER -fwide-types ${CMAKE_CURRENT_SOURCE_DIR}/rrc.asn1
	COMMAND cmake -E touch openbts-umts-asn-generated
	WORKING_DIRECTORY ${generated_dir}
	DEPENDS rrc.asn1
)

add_custom_target(openbts-umts-asn-generated
	DEPENDS ${asn_files_common_o} ${asn_files_rrl_o}
)

include_directories(${generated_dir})

add_library(openbts-umts-asn-common ${asn_files_common_o})
add_dependencies(openbts-umts-asn-common openbts-umts-asn-generated)

add_library(openbts-umts-asn ${asn_files_rrl_o})
target_link_libraries(openbts-umts-asn openbts-umts-asn-common)
add_dependencies(openbts-umts-asn openbts-umts-asn-generated)
