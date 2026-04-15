// ---------------------------------------
// (c) 2026 Eduardo Doria.
// Based on: https://github.com/oviano/sokol-multithread
// ---------------------------------------

#include <string>
#include <cstring>
#include <algorithm>
#include <cassert>

#include "SokolCmdQueue.h"

using namespace doriax;

// ----------------------------------------------------------------------------------------------------

std::vector<SokolRenderCommand> SokolCmdQueue::m_commands[2];
int32_t SokolCmdQueue::m_pending_commands_index = 0;
int32_t SokolCmdQueue::m_commit_commands_index = 1;
std::vector<SokolRenderCleanup> SokolCmdQueue::m_cleanups;
Semaphore SokolCmdQueue::m_update_semaphore;
Semaphore SokolCmdQueue::m_render_semaphore;
std::atomic<bool> SokolCmdQueue::m_flushing = false;
std::atomic<bool> SokolCmdQueue::m_commited = false;
std::mutex SokolCmdQueue::m_execute_mutex;
int32_t SokolCmdQueue::m_frame_index = 0;




// ----------------------------------------------------------------------------------------------------

constexpr int32_t INITIAL_NUMBER_OF_COMMANDS = 256;
constexpr int32_t INITIAL_NUMBER_OF_CLEANUPS = 64;

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::start(){
	// loop through commands
	for (int32_t i = 0; i < 2; i ++)
	{
		// reserve commands
		m_commands[i].reserve(INITIAL_NUMBER_OF_COMMANDS);
	}

	// reserve cleamups
	m_cleanups.reserve(INITIAL_NUMBER_OF_CLEANUPS);
	
	// release render semaphore
	m_render_semaphore.release();
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::finish(){
	// process cleanups
	process_cleanups(-1);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::execute_commands(bool resource_only)
{
	// increase frame index
	m_frame_index ++;

	// not commited? exit
	if (!m_commited)
	{
		return;
	}

	// not flushing?
	if (!m_flushing)
	{
		// acquire update semaphore
		m_update_semaphore.acquire();
	}
	
	{
		// lock execute mutex
		std::scoped_lock<std::mutex> lock(m_execute_mutex);

		// loop through commands
		for (const auto& command : m_commands[m_commit_commands_index])
		{
			// ignore command?
			if (resource_only && !(command.type >= SokolRenderCommand::TYPE::MAKE_BUFFER && command.type <= SokolRenderCommand::TYPE::DESTROY_ATTACHMENTS))
			{
				continue;
			}

			// execute command
			switch (command.type)
			{
			case SokolRenderCommand::TYPE::PUSH_DEBUG_GROUP:
				sg_push_debug_group(command.push_debug_group.name);
				break;
			case SokolRenderCommand::TYPE::POP_DEBUG_GROUP:
				sg_pop_debug_group();
				break;
			case SokolRenderCommand::TYPE::MAKE_BUFFER:
				sg_init_buffer(command.make_buffer.buffer, command.make_buffer.desc);
				break;
			case SokolRenderCommand::TYPE::MAKE_IMAGE:
				sg_init_image(command.make_image.image, command.make_image.desc);
				break;
			case SokolRenderCommand::TYPE::MAKE_SAMPLER:
				sg_init_sampler(command.make_sampler.sampler, command.make_sampler.desc);
				break;
			case SokolRenderCommand::TYPE::MAKE_SHADER:
				sg_init_shader(command.make_shader.shader, command.make_shader.desc);
				break;
			case SokolRenderCommand::TYPE::MAKE_PIPELINE:
				sg_init_pipeline(command.make_pipeline.pipeline, command.make_pipeline.desc);
				break;
			case SokolRenderCommand::TYPE::MAKE_ATTACHMENTS:
				sg_init_attachments(command.make_attachments.attachments, command.make_attachments.desc);
				break;
			case SokolRenderCommand::TYPE::DESTROY_BUFFER:
				sg_uninit_buffer(command.destroy_buffer.buffer);
				break;
			case SokolRenderCommand::TYPE::DESTROY_IMAGE:
				sg_uninit_image(command.destroy_image.image);
				break;
			case SokolRenderCommand::TYPE::DESTROY_SAMPLER:
				sg_uninit_sampler(command.destroy_sampler.sampler);
				break;
			case SokolRenderCommand::TYPE::DESTROY_SHADER:
				sg_uninit_shader(command.destroy_shader.shader);
				break;
			case SokolRenderCommand::TYPE::DESTROY_PIPELINE:
				sg_uninit_pipeline(command.destroy_pipeline.pipeline);
				break;
			case SokolRenderCommand::TYPE::DESTROY_ATTACHMENTS:
				sg_uninit_attachments(command.destroy_attachments.attachments);
				break;
			case SokolRenderCommand::TYPE::UPDATE_BUFFER:
				sg_update_buffer(command.update_buffer.buffer, command.update_buffer.data);
				break;
			case SokolRenderCommand::TYPE::APPEND_BUFFER:
				sg_append_buffer(command.append_buffer.buffer, command.append_buffer.data);
				break;
			case SokolRenderCommand::TYPE::UPDATE_IMAGE:
				sg_update_image(command.update_image.image, command.update_image.data);
				break;
			case SokolRenderCommand::TYPE::BEGIN_PASS:
				sg_begin_pass(command.begin_pass.pass);
				break;
			case SokolRenderCommand::TYPE::APPLY_VIEWPORT:
				sg_apply_viewport(command.apply_viewport.x, command.apply_viewport.y, command.apply_viewport.width, command.apply_viewport.height, command.apply_viewport.origin_top_left);
				break;
			case SokolRenderCommand::TYPE::APPLY_SCISSOR_RECT:
				sg_apply_scissor_rect(command.apply_scissor_rect.x, command.apply_scissor_rect.y, command.apply_scissor_rect.width, command.apply_scissor_rect.height, command.apply_scissor_rect.origin_top_left);
				break;
			case SokolRenderCommand::TYPE::APPLY_PIPELINE:
				sg_apply_pipeline(command.apply_pipeline.pipeline);
				break;
			case SokolRenderCommand::TYPE::APPLY_BINDINGS:
				sg_apply_bindings(command.apply_bindings.bindings);
				break;
			case SokolRenderCommand::TYPE::APPLY_UNIFORMS:
				sg_apply_uniforms(command.apply_uniforms.ub_slot, { command.apply_uniforms.buf, command.apply_uniforms.data_size });
				break;
			case SokolRenderCommand::TYPE::DRAW:
				sg_draw(command.draw.base_element, command.draw.number_of_elements, command.draw.number_of_instances);
				break;
			case SokolRenderCommand::TYPE::END_PASS:
				sg_end_pass();
				break;
			case SokolRenderCommand::TYPE::COMMIT:
				sg_commit();
				break;
			case SokolRenderCommand::TYPE::CUSTOM:
				command.custom.custom_cb(command.custom.custom_data);
				break;
			case SokolRenderCommand::TYPE::NOT_SET:
				break;
			}
		}
	}

	// process cleanups
	process_cleanups(m_frame_index);

	m_commited = false;

	// release render semaphore
	m_render_semaphore.release();
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::wait_for_flush()
{
	// initialise finished flushing
	bool finished_flushing = false;

	// wait for flush
	while (!finished_flushing)
	{
		// not flushing?
		if (!m_flushing)
		{
			// acquire update semaphore
			m_update_semaphore.acquire();
		}
		
		{
			// lock execute mutex
			std::scoped_lock<std::mutex> lock(m_execute_mutex);
			
			// loop through commands
			for (const auto& command : m_commands[m_commit_commands_index])
			{
				// execute command
				switch (command.type)
				{
				case SokolRenderCommand::TYPE::MAKE_BUFFER:
					//sg_init_buffer(command.make_buffer.buffer, command.make_buffer.desc);
					break;
				case SokolRenderCommand::TYPE::MAKE_IMAGE:
					//sg_init_image(command.make_image.image, command.make_image.desc);
					break;
				case SokolRenderCommand::TYPE::MAKE_SAMPLER:
					//sg_init_sampler(command.make_sampler.sampler, command.sampler.desc);
					break;
				case SokolRenderCommand::TYPE::MAKE_SHADER:
					//sg_init_shader(command.make_shader.shader, command.make_shader.desc);
					break;
				case SokolRenderCommand::TYPE::MAKE_PIPELINE:
					//sg_init_pipeline(command.make_pipeline.pipeline, command.make_pipeline.desc);
					break;
				case SokolRenderCommand::TYPE::MAKE_ATTACHMENTS:
					//sg_init_attachments(command.make_attachments.attachments, command.make_attachments.desc);
					break;
				case SokolRenderCommand::TYPE::DESTROY_BUFFER:
					sg_uninit_buffer(command.destroy_buffer.buffer);
					break;
				case SokolRenderCommand::TYPE::DESTROY_IMAGE:
					sg_uninit_image(command.destroy_image.image);
					break;
				case SokolRenderCommand::TYPE::DESTROY_SAMPLER:
					sg_uninit_sampler(command.destroy_sampler.sampler);
					break;
				case SokolRenderCommand::TYPE::DESTROY_SHADER:
					sg_uninit_shader(command.destroy_shader.shader);
					break;
				case SokolRenderCommand::TYPE::DESTROY_PIPELINE:
					sg_uninit_pipeline(command.destroy_pipeline.pipeline);
					break;
				case SokolRenderCommand::TYPE::DESTROY_ATTACHMENTS:
					sg_uninit_attachments(command.destroy_attachments.attachments);
					break;
				default:
					break;
				}
			}
		}
		
		// udpate finished flushing
		finished_flushing = m_flushing;

		// release render semaphore
		m_render_semaphore.release();
	}
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_push_debug_group(const char* name)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::PUSH_DEBUG_GROUP);

	// copy args
	command.push_debug_group.name = name;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_pop_debug_group()
{
	// add command
	/*SokolRenderCommand& command = */m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::POP_DEBUG_GROUP);
}

// ----------------------------------------------------------------------------------------------------

sg_buffer SokolCmdQueue::add_command_make_buffer(const sg_buffer_desc& desc)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::MAKE_BUFFER);

	// copy args
	command.make_buffer.desc = desc;
	
	// alloc buffer
	command.make_buffer.buffer = sg_alloc_buffer();
	
	// return buffer
	return command.make_buffer.buffer;
}

// ----------------------------------------------------------------------------------------------------

sg_image SokolCmdQueue::add_command_make_image(const sg_image_desc& desc)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::MAKE_IMAGE);

	// copy args
	command.make_image.desc = desc;

	// alloc image
	command.make_image.image = sg_alloc_image();
	
	// return image
	return command.make_image.image;
}

// ----------------------------------------------------------------------------------------------------

sg_sampler SokolCmdQueue::add_command_make_sampler(const sg_sampler_desc& desc)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::MAKE_SAMPLER);

	// copy args
	command.make_sampler.desc = desc;

	// alloc sampler
	command.make_sampler.sampler = sg_alloc_sampler();

	// return sampler
	return command.make_sampler.sampler;
}

// ----------------------------------------------------------------------------------------------------

sg_shader SokolCmdQueue::add_command_make_shader(const sg_shader_desc& desc)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::MAKE_SHADER);

	// copy args
	command.make_shader.desc = desc;

	// alloc shader
	command.make_shader.shader = sg_alloc_shader();
	
	// return shader
	return command.make_shader.shader;
}

// ----------------------------------------------------------------------------------------------------

sg_pipeline SokolCmdQueue::add_command_make_pipeline(const sg_pipeline_desc& desc)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::MAKE_PIPELINE);

	// copy args
	command.make_pipeline.desc = desc;

	// alloc pipeline
	command.make_pipeline.pipeline = sg_alloc_pipeline();
	
	// return pipeline
	return command.make_pipeline.pipeline;
}

// ----------------------------------------------------------------------------------------------------

sg_attachments SokolCmdQueue::add_command_make_attachments(const sg_attachments_desc& desc)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::MAKE_ATTACHMENTS);

	// copy args
	command.make_attachments.desc = desc;

	// alloc attachments
	command.make_attachments.attachments = sg_alloc_attachments();
	
	// return attachments
	return command.make_attachments.attachments;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_destroy_buffer(sg_buffer buffer)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::DESTROY_BUFFER);

	// copy args
	command.destroy_buffer.buffer = buffer;

	// schedule cleanup
	schedule_cleanup(dealloc_buffer_cb, (void*)(uintptr_t)command.destroy_buffer.buffer.id);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_destroy_image(sg_image image)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::DESTROY_IMAGE);

	// copy args
	command.destroy_image.image = image;

	// schedule cleanup
	schedule_cleanup(dealloc_image_cb, (void*)(uintptr_t)command.destroy_image.image.id);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_destroy_sampler(sg_sampler sampler)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::DESTROY_SAMPLER);

	// copy args
	command.destroy_sampler.sampler = sampler;

	// schedule cleanup
	schedule_cleanup(dealloc_sampler_cb, (void*)(uintptr_t)command.destroy_sampler.sampler.id);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_destroy_shader(sg_shader shader)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::DESTROY_SHADER);

	// copy args
	command.destroy_shader.shader = shader;

	// schedule cleanup
	schedule_cleanup(dealloc_shader_cb, (void*)(uintptr_t)command.destroy_shader.shader.id);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_destroy_pipeline(sg_pipeline pipeline)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::DESTROY_PIPELINE);

	// copy args
	command.destroy_pipeline.pipeline = pipeline;

	// schedule cleanup
	schedule_cleanup(dealloc_pipeline_cb, (void*)(uintptr_t)command.destroy_pipeline.pipeline.id);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_destroy_attachments(sg_attachments atts)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::DESTROY_ATTACHMENTS);

	// copy args
	command.destroy_attachments.attachments = atts;

	// schedule cleanup
	schedule_cleanup(dealloc_attachments_cb, (void*)(uintptr_t)command.destroy_attachments.attachments.id);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_update_buffer(sg_buffer buffer, const sg_range& data)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::UPDATE_BUFFER);

	// copy args
	command.update_buffer.buffer = buffer;
	command.update_buffer.data = data;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_append_buffer(sg_buffer buffer, const sg_range& data)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::APPEND_BUFFER);

	// copy args
	command.append_buffer.buffer = buffer;
	command.append_buffer.data = data;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_update_image(sg_image image, const sg_image_data& data)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::UPDATE_IMAGE);

	// copy args
	command.update_image.image = image;
	command.update_image.data = data;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_begin_pass(const sg_pass& pass)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::BEGIN_PASS);

	// copy args
	command.begin_pass.pass = pass;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_apply_viewport(int x, int y, int width, int height, bool origin_top_left)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::APPLY_VIEWPORT);

	// copy args
	command.apply_viewport.x = x;
	command.apply_viewport.y = y;
	command.apply_viewport.width = width;
	command.apply_viewport.height = height;
	command.apply_viewport.origin_top_left = origin_top_left;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_apply_scissor_rect(int x, int y, int width, int height, bool origin_top_left)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::APPLY_SCISSOR_RECT);

	// copy args
	command.apply_scissor_rect.x = x;
	command.apply_scissor_rect.y = y;
	command.apply_scissor_rect.width = width;
	command.apply_scissor_rect.height = height;
	command.apply_scissor_rect.origin_top_left = origin_top_left;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_apply_pipeline(sg_pipeline pipeline)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::APPLY_PIPELINE);

	// copy args
	command.apply_pipeline.pipeline = pipeline;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_apply_bindings(const sg_bindings& bindings)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::APPLY_BINDINGS);

	// copy args
	command.apply_bindings.bindings = bindings;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_apply_uniforms(int ub_slot, const sg_range& data)
{
	// data size too big?
	assert((size_t)data.size <= sizeof(SokolRenderCommand::apply_uniforms.buf));

	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::APPLY_UNIFORMS);

	// copy args
	command.apply_uniforms.ub_slot = ub_slot;
	memcpy(command.apply_uniforms.buf, data.ptr, data.size);
	command.apply_uniforms.data_size = data.size;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_draw(int base_element, int number_of_elements, int number_of_instances)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::DRAW);

	// copy args
	command.draw.base_element = base_element;
	command.draw.number_of_elements = number_of_elements;
	command.draw.number_of_instances = number_of_instances;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_end_pass()
{
	// add command
	/*SokolRenderCommand& command = */m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::END_PASS);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_commit()
{
	// add command
	/*SokolRenderCommand& command = */m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::COMMIT);
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::add_command_custom(void (*custom_cb)(void* custom_data), void* custom_data)
{
	// add command
	SokolRenderCommand& command = m_commands[m_pending_commands_index].emplace_back(SokolRenderCommand::TYPE::CUSTOM);

	// copy args
	command.custom.custom_cb = custom_cb;
	command.custom.custom_data = custom_data;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::schedule_cleanup(void (*cleanup_cb)(void* cleanup_data), void* cleanup_data, int32_t number_of_frames_to_defer)
{
	// add cleanup
	SokolRenderCleanup& cleanup = m_cleanups.emplace_back(cleanup_cb, cleanup_data);
	
	// set frame index
	cleanup.frame_index = m_frame_index + 1 + number_of_frames_to_defer;
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::commit_commands()
{
	// acquire render semaphore
	m_render_semaphore.acquire();
	
	// clear commands
	m_commands[m_commit_commands_index].resize(0);
	
	// swap commands indexes
	std::swap(m_pending_commands_index, m_commit_commands_index);

	// mark as commited
	m_commited = true;

	// release update semaphore
	m_update_semaphore.release();
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::flush_commands()
{
	// acquire render semaphore
	m_render_semaphore.acquire();
	
	// clear commands
	m_commands[m_commit_commands_index].resize(0);
	
	// swap commands indexes
	std::swap(m_pending_commands_index, m_commit_commands_index);

	// set flushing
	m_flushing = true;

	// release update semaphore
	m_update_semaphore.release();
}

// ----------------------------------------------------------------------------------------------------

void SokolCmdQueue::process_cleanups(int32_t frame_index)
{
	// loop through cleanups
	for (auto& cleanup : m_cleanups)
	{
		// call cleanup cb?
		if ((cleanup.frame_index < frame_index || frame_index < 0) && cleanup.cleanup_cb)
		{
			// call cleanup cb
			cleanup.cleanup_cb(cleanup.cleanup_data);
			
			// reset cleanup cb
			cleanup.cleanup_cb = nullptr;
		}
	}
	
	// erase invalid cleanups
	m_cleanups.erase(std::remove_if(m_cleanups.begin(), m_cleanups.end(), [](const auto& cleanup) { return !cleanup.cleanup_cb; }), m_cleanups.end());
}
