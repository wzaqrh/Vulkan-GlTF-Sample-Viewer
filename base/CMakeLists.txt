file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h" "../external/imgui/*.cpp")
file(GLOB BASE_HEADERS "*.hpp" "*.h")

set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c)

add_library(base STATIC ${BASE_SRC})

target_link_directories(base PUBLIC ${CMAKE_SOURCE_DIR}/libs)

if(WIN32)
	target_link_libraries(base PUBLIC
		debug ${Vulkan_LIBRARY} 
		debug ${WINLIBS} 
		debug ${CMAKE_SOURCE_DIR}/libs/ktx/ktxd.lib
		optimized ${Vulkan_LIBRARY} 
		optimized ${WINLIBS} 
		optimized ${CMAKE_SOURCE_DIR}/libs/ktx/ktx.lib
	)
else(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif(WIN32)