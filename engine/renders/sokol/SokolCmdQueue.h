// ---------------------------------------
// (c) 2026 Eduardo Doria.
// Based on: https://github.com/oviano/sokol-multithread
// ---------------------------------------

#ifndef SokolCmdQueue_H
#define SokolCmdQueue_H

// ----------------------------------------------------------------------------------------------------

#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

#include "sokol_gfx.h"

#include "ThreadUtils.h"

// ----------------------------------------------------------------------------------------------------

namespace doriax {

	struct SokolRenderCommand
	{
		// types
		struct TYPE
		{
			enum ENUM
			{
				NOT_SET = 0,
				
				PUSH_DEBUG_GROUP,
				POP_DEBUG_GROUP,
				
				MAKE_BUFFER,
				MAKE_IMAGE,
				MAKE_SAMPLER,
				MAKE_SHADER,
				MAKE_PIPELINE,
				MAKE_ATTACHMENTS,
				DESTROY_BUFFER,
				DESTROY_IMAGE,
				DESTROY_SAMPLER,
				DESTROY_SHADER,
				DESTROY_PIPELINE,
				DESTROY_ATTACHMENTS,
				UPDATE_BUFFER,
				APPEND_BUFFER,
				UPDATE_IMAGE,

				BEGIN_PASS,
				APPLY_VIEWPORT,
				APPLY_SCISSOR_RECT,
				APPLY_PIPELINE,
				APPLY_BINDINGS,
				APPLY_UNIFORMS,
				DRAW,
				END_PASS,
				COMMIT,
				
				CUSTOM
			};
		};
		
		SokolRenderCommand() {}
		SokolRenderCommand(TYPE::ENUM _type) : type(_type) {}

		TYPE::ENUM type = TYPE::NOT_SET;

		union
		{
			struct
			{
				const char* name;
			} push_debug_group;

			struct
			{
				sg_buffer_desc desc;
				sg_buffer buffer;
			} make_buffer;
			
			struct
			{
				sg_image_desc desc;
				sg_image image;
			} make_image;

			struct
			{
				sg_sampler_desc desc;
				sg_sampler sampler;
			} make_sampler;
			
			struct
			{
				sg_shader_desc desc;
				sg_shader shader;
			} make_shader;
			
			struct
			{
				sg_pipeline_desc desc;
				sg_pipeline pipeline;
			} make_pipeline;
			
			struct
			{
				sg_attachments_desc desc;
				sg_attachments attachments;
			} make_attachments;
			
			struct
			{
				sg_buffer buffer;
			} destroy_buffer;
			
			struct
			{
				sg_image image;
			} destroy_image;
			
			struct
			{
				sg_sampler sampler;
			} destroy_sampler;
			
			struct
			{
				sg_shader shader;
			} destroy_shader;
			
			struct
			{
				sg_pipeline pipeline;
			} destroy_pipeline;
			
			struct
			{
				sg_attachments attachments;
			} destroy_attachments;
			
			struct
			{
				sg_buffer buffer;
				sg_range data;
			} update_buffer;
			
			struct
			{
				sg_buffer buffer;
				sg_range data;
			} append_buffer;
			
			struct
			{
				sg_image image;
				sg_image_data data;
			} update_image;
			
			struct
			{
				void (*custom_cb)(void* custom_data);
				void* custom_data;
			} custom;

			struct
			{
				sg_pass pass;
			} begin_pass;

			struct
			{
				int x;
				int y;
				int width;
				int height;
				bool origin_top_left;
			} apply_viewport;
			
			struct
			{
				int x;
				int y;
				int width;
				int height;
				bool origin_top_left;
			} apply_scissor_rect;
			
			struct
			{
				sg_pipeline pipeline;
			} apply_pipeline;
			
			struct
			{
				sg_bindings bindings;
			} apply_bindings;
			
			struct
			{
				int ub_slot;
				size_t data_size;
				char buf[16384];
			} apply_uniforms;
			
			struct
			{
				int base_element;
				int number_of_elements;
				int number_of_instances;
			} draw;
		};
	};

// ----------------------------------------------------------------------------------------------------

	struct SokolRenderCleanup
	{
		SokolRenderCleanup() {}
		SokolRenderCleanup(void (*_cleanup_cb)(void* cleanup_data), void* _cleanup_data) : cleanup_cb(_cleanup_cb), cleanup_data(_cleanup_data) {}
		
		void (*cleanup_cb)(void* cleanup_data) = nullptr;
		void* cleanup_data = nullptr;
		int32_t frame_index = 0;
	};

// ----------------------------------------------------------------------------------------------------

	class SokolCmdQueue
	{
	public:
		static void start();
		static void finish();

		// render thread functions
		static void execute_commands(bool resource_only = false);
		static void wait_for_flush();

		// update thread functions
		static void add_command_push_debug_group(const char* name);
		static void add_command_pop_debug_group();
		
		static sg_buffer add_command_make_buffer(const sg_buffer_desc& desc);
		static sg_image add_command_make_image(const sg_image_desc& desc);
		static sg_sampler add_command_make_sampler(const sg_sampler_desc& desc);
		static sg_shader add_command_make_shader(const sg_shader_desc& desc);
		static sg_pipeline add_command_make_pipeline(const sg_pipeline_desc& desc);
		static sg_attachments add_command_make_attachments(const sg_attachments_desc& desc);
		
		static void add_command_destroy_buffer(sg_buffer buffer);
		static void add_command_destroy_image(sg_image image);
		static void add_command_destroy_sampler(sg_sampler sampler);
		static void add_command_destroy_shader(sg_shader shader);
		static void add_command_destroy_pipeline(sg_pipeline pipeline);
		static void add_command_destroy_attachments(sg_attachments atts);
		
		static void add_command_update_buffer(sg_buffer buffer, const sg_range& data);
		static void add_command_append_buffer(sg_buffer buffer, const sg_range& data);
		static void add_command_update_image(sg_image image, const sg_image_data& data);
		
		static void add_command_begin_pass(const sg_pass& pass);
		static void add_command_apply_viewport(int x, int y, int width, int height, bool origin_top_left);
		static void add_command_apply_scissor_rect(int x, int y, int width, int height, bool origin_top_left);
		static void add_command_apply_pipeline(sg_pipeline pipeline);
		static void add_command_apply_bindings(const sg_bindings& bindings);
		static void add_command_apply_uniforms(int ub_slot, const sg_range& data);
		static void add_command_draw(int base_element, int number_of_elements, int number_of_instances);
		static void add_command_end_pass();
		static void add_command_commit();

		static void add_command_custom(void (*custom_cb)(void* custom_data), void* custom_data);
		
		static void schedule_cleanup(void (*cleanup_cb)(void* cleanup_data), void* cleanup_data, int32_t number_of_frames_to_defer = 0);

		static void commit_commands();
		static void flush_commands();
		
		static void lock_execute_mutex() { m_execute_mutex.lock(); }
		static void unlock_execute_mutex() { m_execute_mutex.unlock(); }

		//static sg_pixel_format get_pixel_format() const { return sg_query_desc().context.color_format; }
		
	private:
		static void process_cleanups(int32_t frame_index);

		static void dealloc_buffer_cb(void* cleanup_data) { sg_dealloc_buffer({(uint32_t)(uintptr_t)cleanup_data}); }
		static void dealloc_image_cb(void* cleanup_data) { sg_dealloc_image({(uint32_t)(uintptr_t)cleanup_data}); }
		static void dealloc_sampler_cb(void* cleanup_data) { sg_dealloc_sampler({(uint32_t)(uintptr_t)cleanup_data}); }
		static void dealloc_shader_cb(void* cleanup_data) { sg_dealloc_shader({(uint32_t)(uintptr_t)cleanup_data}); }
		static void dealloc_pipeline_cb(void* cleanup_data) { sg_dealloc_pipeline({(uint32_t)(uintptr_t)cleanup_data}); }
		static void dealloc_attachments_cb(void* cleanup_data) { sg_dealloc_attachments({(uint32_t)(uintptr_t)cleanup_data}); }

		static std::vector<SokolRenderCommand> m_commands[2];
		static int32_t m_pending_commands_index;
		static int32_t m_commit_commands_index;
		static std::vector<SokolRenderCleanup> m_cleanups;

		static Semaphore m_update_semaphore;
		static Semaphore m_render_semaphore;
		static std::atomic<bool> m_flushing;
		static std::atomic<bool> m_commited;
		static std::mutex m_execute_mutex;
		static int32_t m_frame_index;
	};

// ----------------------------------------------------------------------------------------------------

}

#endif // SokolCmdQueue_H
