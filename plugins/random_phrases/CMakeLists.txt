add_custom_target(
		random_phrases ALL
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Copying random_phrases plugin files"
)
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR} DESTINATION ${CMAKE_INSTALL_PREFIX}/plugins)
