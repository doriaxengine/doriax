# Box2D - https://github.com/erincatto/box2d/

include_directories ("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories ("${CMAKE_CURRENT_SOURCE_DIR}/src")

set(BOX2D_SOURCE_FILES
	./src/aabb.c
	./src/aabb.h
	./src/arena_allocator.c
	./src/arena_allocator.h
	./src/bitset.c
	./src/bitset.h
	./src/body.c
	./src/body.h
	./src/broad_phase.c
	./src/broad_phase.h
	./src/constraint_graph.c
	./src/constraint_graph.h
	./src/contact_solver.c
	./src/contact_solver.h
	./src/contact_solver_w4.c
	./src/contact_solver_w8.c
	./src/contact_solver_wide.inl
	./src/contact.c
	./src/contact.h
	./src/container.h
	./src/core.c
	./src/core.h
	./src/distance_joint.c
	./src/distance.c
	./src/dynamic_tree.c
	./src/dynamic_tree.h
	./src/geometry.c
	./src/hull.c
	./src/id_pool.c
	./src/id_pool.h
	./src/island.c
	./src/island.h
	./src/joint.c
	./src/joint.h
	./src/manifold.c
	./src/math_functions.c
	./src/motor_joint.c
	./src/mover_joint.c
	./src/mover.c
	./src/parallel_for.c
	./src/parallel_for.h
	./src/physics_world.c
	./src/physics_world.h
	./src/platform.h
	./src/pogo_joint.c
	./src/prismatic_joint.c
	./src/recording_ops.inl
	./src/recording.c
	./src/recording.h
	./src/replay.c
	./src/replay.h
	./src/revolute_joint.c
	./src/scheduler.c
	./src/scheduler.h
	./src/sensor.c
	./src/sensor.h
	./src/shape.c
	./src/shape.h
	./src/simd.c
	./src/simd.h
	./src/snapshot.c
	./src/snapshot.h
	./src/solver_set.c
	./src/solver_set.h
	./src/solver.c
	./src/solver.h
	./src/table.c
	./src/table.h
	./src/timer.c
	./src/types.c
	./src/weld_joint.c
	./src/wheel_joint.c
)

set(BOX2D_API_FILES
	./include/box2d/base.h
	./include/box2d/box2d.h
	./include/box2d/collision.h
	./include/box2d/config.h
	./include/box2d/constants.h
	./include/box2d/id.h
	./include/box2d/math_functions.h
	./include/box2d/math_types.h
	./include/box2d/types.h
)

add_library(

    box2d

    STATIC

    ${BOX2D_SOURCE_FILES}
	${BOX2D_API_FILES}
)

find_package(Threads REQUIRED)
target_link_libraries(box2d PRIVATE Threads::Threads)

if(EMSCRIPTEN AND EMSCRIPTEN_THREAD_SUPPORT)
	target_compile_options(box2d PRIVATE -pthread)
endif()

# part of Box2D CMakeLists.txt configuration

# Deterministic math
# https://box2d.org/posts/2024/08/determinism/
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	if (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
		target_compile_options(box2d PRIVATE /clang:-ffp-contract=off)
	else()
		target_compile_options(box2d PRIVATE -ffp-contract=off)
	endif()
endif()


# part of Box2D src/CMakeLists.txt configuration

set_target_properties(
	box2d

	PROPERTIES

	C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
	C_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN YES
)

if (BOX2D_PROFILE)
	target_compile_definitions(box2d PRIVATE BOX2D_PROFILE)
	set(TRACY_DELAYED_INIT ON CACHE BOOL "Enable delayed init for Tracy" FORCE)
	set(TRACY_MANUAL_LIFETIME ON CACHE BOOL "Enable manual lifetime for Tracy" FORCE)

	FetchContent_Declare(
		tracy
		GIT_REPOSITORY https://github.com/wolfpld/tracy.git
		GIT_TAG v0.13.1
		GIT_SHALLOW TRUE
		GIT_PROGRESS TRUE
	)
	FetchContent_MakeAvailable(tracy)

	target_link_libraries(box2d PUBLIC TracyClient)
endif()

if (BOX2D_VALIDATE)
	target_compile_definitions(box2d PRIVATE BOX2D_VALIDATE)
endif()

if (BOX2D_DISABLE_SIMD)
	target_compile_definitions(box2d PRIVATE BOX2D_DISABLE_SIMD)
endif()

if (MSVC)
	message(STATUS "Box2D on MSVC")	
	if (BUILD_SHARED_LIBS)
		# this is needed by DLL users to import Box2D symbols
		target_compile_definitions(box2d INTERFACE BOX2D_DLL)
	endif()

	# Visual Studio won't load the natvis unless it is in the project
	target_sources(box2d PRIVATE ./src/box2d.natvis)

	# Enable asserts in release with debug info
	target_compile_definitions(box2d PUBLIC "$<$<CONFIG:RELWITHDEBINFO>:B2_ENABLE_ASSERT>")

	# All warnings
	target_compile_options(box2d PRIVATE /W4)

	# AVX2 is selected at runtime
	if (NOT BOX2D_DISABLE_SIMD AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X86)$")
		set_source_files_properties(./src/contact_solver_w8.c PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
	endif()

elseif (MINGW)
	message(STATUS "Box2D on MinGW")
elseif (APPLE)
	message(STATUS "Box2D on Apple")
elseif (EMSCRIPTEN)
	message(STATUS "Box2D on Emscripten")
	if (NOT BOX2D_DISABLE_SIMD)
		target_compile_options(box2d PRIVATE -msimd128 -msse2)
	endif()
elseif (UNIX)
	message(STATUS "Box2D using Unix")
endif()
