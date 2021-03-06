/*
*	Copyright 2015-2016 Tomas Mikalauskas. All rights reserved.
*	GitHub repository - https://github.com/TywyllSoftware/TywRenderer
*	This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/
#include <RendererPch\stdafx.h>

//Renderer Includes
#include "VKRenderer.h"
#include "ThirdParty\ImGui\imgui.h"



#define IMGUI_VK_QUEUED_FRAMES 2


// Vulkan Data
//static VkAllocationCallbacks* g_Allocator = NULL;
static VkPhysicalDevice       g_Gpu = VK_NULL_HANDLE;
static VkDevice               g_Device = VK_NULL_HANDLE;
static VkRenderPass           g_RenderPass = VK_NULL_HANDLE;
static VkPipelineCache        g_PipelineCache = VK_NULL_HANDLE;
static VkDescriptorPool       g_DescriptorPool = VK_NULL_HANDLE;
static void(*g_CheckVkResult)(VkResult err) = NULL;

static VkFormat               g_Format = VK_FORMAT_B8G8R8A8_UNORM;
static VkCommandBuffer        g_CommandBuffer = VK_NULL_HANDLE;
static size_t                 g_BufferMemoryAlignment = 256;
static VkPipelineCreateFlags  g_PipelineCreateFlags = 0;
static int                    g_FrameIndex = 0;

static VkDescriptorSetLayout  g_DescriptorSetLayout = VK_NULL_HANDLE;
static VkPipelineLayout       g_PipelineLayout = VK_NULL_HANDLE;
static VkDescriptorSet        g_DescriptorSet = VK_NULL_HANDLE;
static VkPipeline             g_Pipeline = VK_NULL_HANDLE;

static VkSampler              g_FontSampler = VK_NULL_HANDLE;
static VkDeviceMemory         g_FontMemory = VK_NULL_HANDLE;
static VkImage                g_FontImage = VK_NULL_HANDLE;
static VkImageView            g_FontView = VK_NULL_HANDLE;

static VkDeviceMemory         g_VertexBufferMemory[IMGUI_VK_QUEUED_FRAMES] = {};
static VkDeviceMemory         g_IndexBufferMemory[IMGUI_VK_QUEUED_FRAMES] = {};
static size_t                 g_VertexBufferSize[IMGUI_VK_QUEUED_FRAMES] = {};
static size_t                 g_IndexBufferSize[IMGUI_VK_QUEUED_FRAMES] = {};
static VkBuffer               g_VertexBuffer[IMGUI_VK_QUEUED_FRAMES] = {};
static VkBuffer               g_IndexBuffer[IMGUI_VK_QUEUED_FRAMES] = {};

static VkDeviceMemory         g_UploadBufferMemory = VK_NULL_HANDLE;
static VkBuffer               g_UploadBuffer = VK_NULL_HANDLE;

static VkClearValue					g_ClearValue[2] = { { 0.5f, 0.5f, 0.5f, 1.0f },{ 1.0f, 0 } };
static std::vector<VkFramebuffer*>	g_FrameBuffers;
static uint32_t*					g_FrameBufferWidth;
static uint32_t*					g_FrameBufferHeight;

static uint32_t						g_MousePosX;
static uint32_t						g_MousePosY;
static bool							g_bMouseOnWindows = false;
static bool							g_bMousePressed = false;



// Data
static double       g_Time = 0.0f;
static bool         g_MousePressed[3] = { false, false, false };
static float        g_MouseWheel = 0.0f;
static int          g_ShaderHandle = 0, g_VertHandle = 0, g_FragHandle = 0;
static int          g_AttribLocationTex = 0, g_AttribLocationProjMtx = 0;
static int          g_AttribLocationPosition = 0, g_AttribLocationUV = 0, g_AttribLocationColor = 0;
static unsigned int g_VboHandle = 0, g_VaoHandle = 0, g_ElementsHandle = 0;




static uint32_t ImGui_ImplGlfwVulkan_MemoryType(VkMemoryPropertyFlags properties, uint32_t type_bits)
{
	VkPhysicalDeviceMemoryProperties prop;
	vkGetPhysicalDeviceMemoryProperties(g_Gpu, &prop);
	for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
	{
		if ((prop.memoryTypes[i].propertyFlags & properties) == properties && type_bits & (1 << i))
		{
			return i;
		}
	}
	return 0xffffffff; // Unable to find memoryType
}


// This is the main rendering function that you have to implement and provide to ImGui (via setting up 'RenderDrawListsFn' in the ImGuiIO structure)
// If text or lines are blurry when integrating ImGui in your engine:
// - in your Render function, try translating your projection matrix by (0.5f,0.5f) or (0.375f,0.375f)
void ImGui_ImplGlfwGL3_RenderDrawLists(ImDrawData* draw_data)
{
	ImGuiIO& io = ImGui::GetIO();

	// Create the Vertex Buffer:
	size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
	if (!g_VertexBuffer[g_FrameIndex] || g_VertexBufferSize[g_FrameIndex] < vertex_size)
	{
		if (g_VertexBuffer[g_FrameIndex])
		{
			vkDestroyBuffer(g_Device, g_VertexBuffer[g_FrameIndex], nullptr);
		}
		if (g_VertexBufferMemory[g_FrameIndex])
		{
			vkFreeMemory(g_Device, g_VertexBufferMemory[g_FrameIndex], nullptr);
		}

		size_t vertex_buffer_size = ((vertex_size - 1) / g_BufferMemoryAlignment + 1) * g_BufferMemoryAlignment;
		VkBufferCreateInfo buffer_info = {};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = vertex_buffer_size;
		buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(g_Device, &buffer_info, nullptr, &g_VertexBuffer[g_FrameIndex]));
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(g_Device, g_VertexBuffer[g_FrameIndex], &req);
		g_BufferMemoryAlignment = (g_BufferMemoryAlignment > req.alignment) ? g_BufferMemoryAlignment : req.alignment;
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = req.size;
		alloc_info.memoryTypeIndex = ImGui_ImplGlfwVulkan_MemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
		VK_CHECK_RESULT(vkAllocateMemory(g_Device, &alloc_info, nullptr, &g_VertexBufferMemory[g_FrameIndex]));
		VK_CHECK_RESULT(vkBindBufferMemory(g_Device, g_VertexBuffer[g_FrameIndex], g_VertexBufferMemory[g_FrameIndex], 0));
		g_VertexBufferSize[g_FrameIndex] = vertex_buffer_size;
	}

	// Create the Index Buffer:
	size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
	if (!g_IndexBuffer[g_FrameIndex] || g_IndexBufferSize[g_FrameIndex] < index_size)
	{
		if (g_IndexBuffer[g_FrameIndex])
		{
			vkDestroyBuffer(g_Device, g_IndexBuffer[g_FrameIndex], nullptr);
		}
		if (g_IndexBufferMemory[g_FrameIndex])
		{
			vkFreeMemory(g_Device, g_IndexBufferMemory[g_FrameIndex], nullptr);
		}

		size_t index_buffer_size = ((index_size - 1) / g_BufferMemoryAlignment + 1) * g_BufferMemoryAlignment;
		VkBufferCreateInfo buffer_info = {};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = index_buffer_size;
		buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(g_Device, &buffer_info, nullptr, &g_IndexBuffer[g_FrameIndex]));
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(g_Device, g_IndexBuffer[g_FrameIndex], &req);
		g_BufferMemoryAlignment = (g_BufferMemoryAlignment > req.alignment) ? g_BufferMemoryAlignment : req.alignment;
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = req.size;
		alloc_info.memoryTypeIndex = ImGui_ImplGlfwVulkan_MemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
		VK_CHECK_RESULT(vkAllocateMemory(g_Device, &alloc_info, nullptr, &g_IndexBufferMemory[g_FrameIndex]));
		VK_CHECK_RESULT(vkBindBufferMemory(g_Device, g_IndexBuffer[g_FrameIndex], g_IndexBufferMemory[g_FrameIndex], 0));
		g_IndexBufferSize[g_FrameIndex] = index_buffer_size;
	}

	// Upload Vertex and index Data:
	{
		ImDrawVert* vtx_dst;
		ImDrawIdx* idx_dst;
		VK_CHECK_RESULT(vkMapMemory(g_Device, g_VertexBufferMemory[g_FrameIndex], 0, vertex_size, 0, (void**)(&vtx_dst)));
		VK_CHECK_RESULT(vkMapMemory(g_Device, g_IndexBufferMemory[g_FrameIndex], 0, index_size, 0, (void**)(&idx_dst)));
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			memcpy(vtx_dst, &cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size() * sizeof(ImDrawVert));
			memcpy(idx_dst, &cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size() * sizeof(ImDrawIdx));
			vtx_dst += cmd_list->VtxBuffer.size();
			idx_dst += cmd_list->IdxBuffer.size();
		}
		VkMappedMemoryRange range[2] = {};
		range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range[0].memory = g_VertexBufferMemory[g_FrameIndex];
		range[0].size = vertex_size;
		range[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range[1].memory = g_IndexBufferMemory[g_FrameIndex];
		range[1].size = index_size;
		VK_CHECK_RESULT(vkFlushMappedMemoryRanges(g_Device, 2, range));
		vkUnmapMemory(g_Device, g_VertexBufferMemory[g_FrameIndex]);
		vkUnmapMemory(g_Device, g_IndexBufferMemory[g_FrameIndex]);
	}

	// Bind pipeline and descriptor sets:
	{
		vkCmdBindPipeline(g_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipeline);
		VkDescriptorSet desc_set[1] = { g_DescriptorSet };
		vkCmdBindDescriptorSets(g_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_PipelineLayout, 0, 1, desc_set, 0, NULL);
	}

	// Bind Vertex And Index Buffer:
	{
		VkBuffer vertex_buffers[1] = { g_VertexBuffer[g_FrameIndex] };
		VkDeviceSize vertex_offset[1] = { 0 };
		vkCmdBindVertexBuffers(g_CommandBuffer, 0, 1, vertex_buffers, vertex_offset);
		vkCmdBindIndexBuffer(g_CommandBuffer, g_IndexBuffer[g_FrameIndex], 0, VK_INDEX_TYPE_UINT16);
	}

	// Setup viewport:
	{
		VkViewport viewport;
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = ImGui::GetIO().DisplaySize.x;
		viewport.height = ImGui::GetIO().DisplaySize.y;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(g_CommandBuffer, 0, 1, &viewport);
	}

	// Setup scale and translation:
	{
		float scale[2];
		scale[0] = 2.0f / io.DisplaySize.x;
		scale[1] = 2.0f / io.DisplaySize.y;
		float translate[2];
		translate[0] = -1.0f;
		translate[1] = -1.0f;
		vkCmdPushConstants(g_CommandBuffer, g_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 0, sizeof(float) * 2, scale);
		vkCmdPushConstants(g_CommandBuffer, g_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);
	}

	// Render the command lists:
	int vtx_offset = 0;
	int idx_offset = 0;
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.size(); cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback)
			{
				pcmd->UserCallback(cmd_list, pcmd);
			}
			else
			{
				VkRect2D scissor;
				scissor.offset.x = static_cast<int32_t>(pcmd->ClipRect.x);
				scissor.offset.y = static_cast<int32_t>(pcmd->ClipRect.y);
				scissor.extent.width = static_cast<uint32_t>(pcmd->ClipRect.z - pcmd->ClipRect.x);
				scissor.extent.height = static_cast<uint32_t>(pcmd->ClipRect.w - pcmd->ClipRect.y + 1); // TODO: + 1??????
				vkCmdSetScissor(g_CommandBuffer, 0, 1, &scissor);
				vkCmdDrawIndexed(g_CommandBuffer, pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
			}
			idx_offset += pcmd->ElemCount;
		}
		vtx_offset += cmd_list->VtxBuffer.size();
	}
}

void ImGui_Render()
{
	// 1. Show a simple window
	// Tip: if we don't call ImGui::Begin()/ImGui::End() the widgets appears in a window automatically called "Debug"
	{
		static float f = 0.0f;
		ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiSetCond_FirstUseEver);
		ImGui::Text("Hello, world!");
		ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
		ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	}
}

void ImGui_ImplGlfwVulkan_Render(VkCommandBuffer command_buffer, uint32_t& bufferIndex)
{
	static VkCommandBufferBeginInfo cmdBufInfo = VkTools::Initializer::CommandBufferBeginInfo();
	g_CommandBuffer = command_buffer;

	//There is problems with it. Settings to proper size. Will cover the actual renderer
	//FIX ME
	VkRenderPassBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	info.pNext = NULL;
	info.renderPass = g_RenderPass;
	info.framebuffer = *g_FrameBuffers[bufferIndex];
	info.renderArea.extent.width = 0;// *g_FrameBufferWidth;
	info.renderArea.extent.height = 0;//*g_FrameBufferHeight;
	info.renderArea.offset.x = 0;
	info.renderArea.offset.y = 0;
	info.clearValueCount = 2;
	info.pClearValues = g_ClearValue;

	//Render
	{
		VK_CHECK_RESULT(vkBeginCommandBuffer(g_CommandBuffer, &cmdBufInfo));
		vkCmdBeginRenderPass(g_CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);

		ImGui::Render();

		vkCmdEndRenderPass(g_CommandBuffer);
		VK_CHECK_RESULT(vkEndCommandBuffer(g_CommandBuffer));
	}

	g_CommandBuffer = VK_NULL_HANDLE;
	g_FrameIndex = (g_FrameIndex + 1) % IMGUI_VK_QUEUED_FRAMES;
}

void ImGui_ImplGlfwVulkan_NewFrame(double current_time)
{
	ImGuiIO& io = ImGui::GetIO();


	//Set display and framebuffer scale
	io.DisplaySize = ImVec2(static_cast<float>(*g_FrameBufferWidth), static_cast<float>(*g_FrameBufferHeight));
	io.DisplayFramebufferScale = ImVec2(1, 1);

	// Setup time step
	io.DeltaTime = current_time;
	

	// Setup inputs
	// (we already got mouse wheel, keyboard keys & characters from glfw callbacks polled in glfwPollEvents())
	if (g_bMouseOnWindows)
	{
		io.MousePos = ImVec2((float)g_MousePosX, (float)g_MousePosY);   // Mouse position in screen coordinates (set to -1,-1 if no mouse / on another screen, etc.)
	}
	else
	{
		io.MousePos = ImVec2(-1, -1);
	}

	for (int i = 0; i < 3; i++)
	{
		//io.MouseDown[i] = g_MousePressed[i] || glfwGetMouseButton(g_Window, i) != 0;    // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
		io.MouseDown[i] = g_bMousePressed;
	}

	//io.MouseWheel = g_MouseWheel;
	//g_MouseWheel = 0.0f;

	// Start the frame
	ImGui::NewFrame();
}


bool ImGui_ImplGlfwVulkan_Init(VulkanRendererInitializer *m_pWRenderer, uint32_t *framebufferwidth,uint32_t *framebufferheight, const std::string& strVertexShaderPath, const std::string& strFragmentShaderPath, bool install_callbacks)
{
	g_Gpu = m_pWRenderer->m_SwapChain.physicalDevice;
	g_Device = m_pWRenderer->m_SwapChain.device;
	g_PipelineCache = m_pWRenderer->m_PipelineCache;
	g_RenderPass = m_pWRenderer->m_RenderPass;
	g_FrameBufferWidth = framebufferwidth;
	g_FrameBufferHeight = framebufferheight;


	g_FrameBuffers.resize(m_pWRenderer->m_FrameBuffers.size());
	for (uint32_t i = 0; i < g_FrameBuffers.size(); i++)
	{
		g_FrameBuffers[i] = &m_pWRenderer->m_FrameBuffers[i];
	}

	
	//Create pool
	VkDescriptorPoolSize pool_size[11] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};
	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000 * 11;
	pool_info.poolSizeCount = 11;
	pool_info.pPoolSizes = pool_size;
	VK_CHECK_RESULT(vkCreateDescriptorPool(g_Device, &pool_info, nullptr, &g_DescriptorPool));

	/*
	//Prepare Render Pass
	VkAttachmentDescription attachment = {};
	attachment.format = g_Format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	VkAttachmentReference color_attachment = {};
	color_attachment.attachment = 0;
	color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment;
	VkRenderPassCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = 1;
	info.pAttachments = &attachment;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	VK_CHECK_RESULT(vkCreateRenderPass(g_Device, &info, nullptr, &g_RenderPass));
	*/
	

	// Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
	ImGuiIO& io = ImGui::GetIO();
	/*
	io.KeyMap[ImGuiKey_Tab] = (int)keyNum_t::K_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = (int)keyNum_t::K_LEFTARROW;
	io.KeyMap[ImGuiKey_RightArrow] = (int)keyNum_t::K_RIGHTARROW;
	io.KeyMap[ImGuiKey_UpArrow] = (int)keyNum_t::K_UPARROW;
	io.KeyMap[ImGuiKey_DownArrow] = (int)keyNum_t::K_DOWNARROW;
	io.KeyMap[ImGuiKey_PageUp] = (int)keyNum_t::K_PGUP;
	io.KeyMap[ImGuiKey_PageDown] = (int)keyNum_t::K_PGDN;
	io.KeyMap[ImGuiKey_Home] = (int)keyNum_t::K_HOME;
	io.KeyMap[ImGuiKey_End] = (int)keyNum_t::K_HOME;
	io.KeyMap[ImGuiKey_Delete] = (int)keyNum_t::K_DEL;
	io.KeyMap[ImGuiKey_Backspace] = (int)keyNum_t::K_BACKSPACE;
	io.KeyMap[ImGuiKey_Enter] = (int)keyNum_t::K_ENTER;
	io.KeyMap[ImGuiKey_Escape] = (int)keyNum_t::K_ESCAPE;
	io.KeyMap[ImGuiKey_A] = (int)keyNum_t::K_A;
	io.KeyMap[ImGuiKey_C] = (int)keyNum_t::K_C;
	io.KeyMap[ImGuiKey_V] = (int)keyNum_t::K_V;
	io.KeyMap[ImGuiKey_X] = (int)keyNum_t::K_X;
	io.KeyMap[ImGuiKey_Y] = (int)keyNum_t::K_Y;
	io.KeyMap[ImGuiKey_Z] = (int)keyNum_t::K_Z;
	*/


	//io.SetClipboardTextFn = ImGui_ImplGlfwVulkan_SetClipboardText;
	//io.GetClipboardTextFn = ImGui_ImplGlfwVulkan_GetClipboardText;
	io.RenderDrawListsFn = ImGui_ImplGlfwGL3_RenderDrawLists;       // Alternatively you can set this to NULL and call ImGui::GetDrawData() after ImGui::Render() to get the same ImDrawData pointer.
#ifdef _WIN32
	io.ImeWindowHandle = m_pWRenderer->GetWind32Handle();
#endif

	ImGui_ImplGlfwVulkan_CreateDeviceObjects(strVertexShaderPath, strFragmentShaderPath);
	return true;
}


bool ImGui_ImplGlfwVulkan_CreateFontsTexture(VkCommandBuffer command_buffer)
{
	ImGuiIO& io = ImGui::GetIO();

	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	size_t upload_size = width*height * 4 * sizeof(char);

	// Create the Image:
	{
		VkImageCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.extent.width = width;
		info.extent.height = height;
		info.extent.depth = 1;
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(g_Device, &info, nullptr, &g_FontImage));
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(g_Device, g_FontImage, &req);
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = req.size;
		alloc_info.memoryTypeIndex = ImGui_ImplGlfwVulkan_MemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, req.memoryTypeBits);
		VK_CHECK_RESULT(vkAllocateMemory(g_Device, &alloc_info, nullptr, &g_FontMemory));
		VK_CHECK_RESULT(vkBindImageMemory(g_Device, g_FontImage, g_FontMemory, 0));
	}

	// Create the Image View:
	{
		VkImageViewCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = g_FontImage;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.layerCount = 1;
		VK_CHECK_RESULT(vkCreateImageView(g_Device, &info, nullptr, &g_FontView));
	}

	// Update the Descriptor Set:
	{
		VkDescriptorImageInfo desc_image[1] = {};
		desc_image[0].sampler = g_FontSampler;
		desc_image[0].imageView = g_FontView;
		desc_image[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkWriteDescriptorSet write_desc[1] = {};
		write_desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_desc[0].dstSet = g_DescriptorSet;
		write_desc[0].descriptorCount = 1;
		write_desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write_desc[0].pImageInfo = desc_image;
		vkUpdateDescriptorSets(g_Device, 1, write_desc, 0, NULL);
	}

	// Create the Upload Buffer:
	{
		VkBufferCreateInfo buffer_info = {};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = upload_size;
		buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(g_Device, &buffer_info, nullptr, &g_UploadBuffer));
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(g_Device, g_UploadBuffer, &req);
		g_BufferMemoryAlignment = (g_BufferMemoryAlignment > req.alignment) ? g_BufferMemoryAlignment : req.alignment;
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = req.size;
		alloc_info.memoryTypeIndex = ImGui_ImplGlfwVulkan_MemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
		VK_CHECK_RESULT(vkAllocateMemory(g_Device, &alloc_info, nullptr, &g_UploadBufferMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(g_Device, g_UploadBuffer, g_UploadBufferMemory, 0));
	}

	// Upload to Buffer:
	{
		char* map = NULL;
		VK_CHECK_RESULT(vkMapMemory(g_Device, g_UploadBufferMemory, 0, upload_size, 0, (void**)(&map)));
		memcpy(map, pixels, upload_size);
		VkMappedMemoryRange range[1] = {};
		range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range[0].memory = g_UploadBufferMemory;
		range[0].size = upload_size;
		VK_CHECK_RESULT(vkFlushMappedMemoryRanges(g_Device, 1, range));
		vkUnmapMemory(g_Device, g_UploadBufferMemory);
	}
	// Copy to Image:
	{
		VkImageMemoryBarrier copy_barrier[1] = {};
		copy_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		copy_barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		copy_barrier[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		copy_barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		copy_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copy_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copy_barrier[0].image = g_FontImage;
		copy_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_barrier[0].subresourceRange.levelCount = 1;
		copy_barrier[0].subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, copy_barrier);

		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageOffset = VkOffset3D{ 0,0,0 };
		region.imageExtent = VkExtent3D{ static_cast<uint32_t>(width),static_cast<uint32_t>(height),1 };
		vkCmdCopyBufferToImage(command_buffer, g_UploadBuffer, g_FontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		VkImageMemoryBarrier use_barrier[1] = {};
		use_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		use_barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		use_barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		use_barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		use_barrier[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		use_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		use_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		use_barrier[0].image = g_FontImage;
		use_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		use_barrier[0].subresourceRange.levelCount = 1;
		use_barrier[0].subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, use_barrier);
	}

	// Store our identifier
	io.Fonts->TexID = (void *)(intptr_t)g_FontImage;

	return true;
}

bool ImGui_ImplGlfwVulkan_CreateDeviceObjects(const std::string& strVertexShaderPath, const std::string& strFragmentShaderPath)
{
	VkShaderModule vert_module;
	VkShaderModule frag_module;


	if (!g_FontSampler)
	{
		VkSamplerCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.minLod = -1000;
		info.maxLod = 1000;
		VK_CHECK_RESULT(vkCreateSampler(g_Device, &info, nullptr, &g_FontSampler));
	}

	if (!g_DescriptorSetLayout)
	{
		VkSampler sampler[1] = { g_FontSampler };
		VkDescriptorSetLayoutBinding binding[1] = {};
		binding[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding[0].descriptorCount = 1;
		binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		binding[0].pImmutableSamplers = sampler;
		VkDescriptorSetLayoutCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.bindingCount = 1;
		info.pBindings = binding;
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(g_Device, &info, nullptr, &g_DescriptorSetLayout));
	}

	// Create Descriptor Set:
	{
		VkDescriptorSetAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc_info.descriptorPool = g_DescriptorPool;
		alloc_info.descriptorSetCount = 1;
		alloc_info.pSetLayouts = &g_DescriptorSetLayout;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(g_Device, &alloc_info, &g_DescriptorSet));
	}

	if (!g_PipelineLayout)
	{
		VkPushConstantRange push_constants[2] = {};
		push_constants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		push_constants[0].offset = sizeof(float) * 0;
		push_constants[0].size = sizeof(float) * 2;
		push_constants[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		push_constants[1].offset = sizeof(float) * 2;
		push_constants[1].size = sizeof(float) * 2;
		VkDescriptorSetLayout set_layout[1] = { g_DescriptorSetLayout };
		VkPipelineLayoutCreateInfo layout_info = {};
		layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_info.setLayoutCount = 1;
		layout_info.pSetLayouts = set_layout;
		layout_info.pushConstantRangeCount = 2;
		layout_info.pPushConstantRanges = push_constants;
		VK_CHECK_RESULT(vkCreatePipelineLayout(g_Device, &layout_info, nullptr, &g_PipelineLayout));
	}

	VkPipelineShaderStageCreateInfo stage[2] = {};
	stage[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stage[0].pName = "main";
#if defined(__ANDROID__)
		shaderStage.module = vkTools::loadShader(androidApp->activity->assetManager, fileName.c_str(), device, stage);
#else
		stage[0].module = VkTools::LoadShader(strVertexShaderPath, g_Device, VK_SHADER_STAGE_VERTEX_BIT);
#endif
	

	stage[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stage[1].pName = "main";
#if defined(__ANDROID__)
	stage[1].module = vkTools::loadShader(androidApp->activity->assetManager, fileName.c_str(), device, stage);
#else
	stage[1].module = VkTools::LoadShader(strFragmentShaderPath, g_Device, VK_SHADER_STAGE_FRAGMENT_BIT);
#endif
	

	VkVertexInputBindingDescription binding_desc[1] = {};
	binding_desc[0].stride = sizeof(ImDrawVert);
	binding_desc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attribute_desc[3] = {};
	attribute_desc[0].location = 0;
	attribute_desc[0].binding = binding_desc[0].binding;
	attribute_desc[0].format = VK_FORMAT_R32G32_SFLOAT;
	attribute_desc[0].offset = (size_t)(&((ImDrawVert*)0)->pos);
	attribute_desc[1].location = 1;
	attribute_desc[1].binding = binding_desc[0].binding;
	attribute_desc[1].format = VK_FORMAT_R32G32_SFLOAT;
	attribute_desc[1].offset = (size_t)(&((ImDrawVert*)0)->uv);
	attribute_desc[2].location = 2;
	attribute_desc[2].binding = binding_desc[0].binding;
	attribute_desc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
	attribute_desc[2].offset = (size_t)(&((ImDrawVert*)0)->col);

	VkPipelineVertexInputStateCreateInfo vertex_info = {};
	vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_info.vertexBindingDescriptionCount = 1;
	vertex_info.pVertexBindingDescriptions = binding_desc;
	vertex_info.vertexAttributeDescriptionCount = 3;
	vertex_info.pVertexAttributeDescriptions = attribute_desc;

	VkPipelineInputAssemblyStateCreateInfo ia_info = {};
	ia_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport_info = {};
	viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_info.viewportCount = 1;
	viewport_info.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster_info = {};
	raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster_info.polygonMode = VK_POLYGON_MODE_FILL;
	raster_info.cullMode = VK_CULL_MODE_NONE;
	raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster_info.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms_info = {};
	ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState color_attachment[1] = {};
	color_attachment[0].blendEnable = VK_TRUE;
	color_attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	color_attachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	color_attachment[0].colorBlendOp = VK_BLEND_OP_ADD;
	color_attachment[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	color_attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	color_attachment[0].alphaBlendOp = VK_BLEND_OP_ADD;
	color_attachment[0].colorWriteMask = 0xf, VK_TRUE;

	VkPipelineColorBlendStateCreateInfo blend_info = {};
	blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend_info.attachmentCount = 1;
	blend_info.pAttachments = color_attachment;

	VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic_state = {};
	dynamic_state.dynamicStateCount = 2;
	dynamic_state.pDynamicStates = dynamic_states;
	dynamic_state.pNext = VK_NULL_HANDLE;
	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
	// Basic depth compare setup with depth writes and depth test enabled
	// No stencil used 
	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilState.depthTestEnable = VK_TRUE;
	depthStencilState.depthWriteEnable = VK_TRUE;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilState.depthBoundsTestEnable = VK_FALSE;
	depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
	depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
	depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
	depthStencilState.stencilTestEnable = VK_FALSE;
	depthStencilState.front = depthStencilState.back;

	VkGraphicsPipelineCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.flags = g_PipelineCreateFlags;
	info.stageCount = 2;
	info.pStages = stage;
	info.pVertexInputState = &vertex_info;
	info.pInputAssemblyState = &ia_info;
	info.pViewportState = &viewport_info;
	info.pRasterizationState = &raster_info;
	info.pMultisampleState = &ms_info;
	info.pColorBlendState = &blend_info;
	info.pDynamicState = &dynamic_state;
	info.pDepthStencilState = &depthStencilState;
	info.layout = g_PipelineLayout;
	info.renderPass = g_RenderPass;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(g_Device, g_PipelineCache, 1, &info, nullptr, &g_Pipeline));

	vkDestroyShaderModule(g_Device, stage[0].module, nullptr);
	vkDestroyShaderModule(g_Device, stage[1].module, nullptr);

	return true;
}

void    ImGui_ImplGlfwVulkan_InvalidateFontUploadObjects()
{
	if (g_UploadBuffer)
	{
		vkDestroyBuffer(g_Device, g_UploadBuffer, nullptr);
		g_UploadBuffer = VK_NULL_HANDLE;
	}
	if (g_UploadBufferMemory)
	{
		vkFreeMemory(g_Device, g_UploadBufferMemory, nullptr);
		g_UploadBufferMemory = VK_NULL_HANDLE;
	}
}

void  ImGui_ImplGlfwVulkan_InvalidateDeviceObjects()
{
	ImGui_ImplGlfwVulkan_InvalidateFontUploadObjects();
	for (int i = 0; i<IMGUI_VK_QUEUED_FRAMES; i++)
	{
		if (g_VertexBuffer[i])
			vkDestroyBuffer(g_Device, g_VertexBuffer[i], nullptr);
		if (g_VertexBufferMemory[i])
			vkFreeMemory(g_Device, g_VertexBufferMemory[i], nullptr);
		if (g_IndexBuffer[i])
			vkDestroyBuffer(g_Device, g_IndexBuffer[i], nullptr);
		if (g_IndexBufferMemory[i])
			vkFreeMemory(g_Device, g_IndexBufferMemory[i], nullptr);
	}

	if (g_FontView)
		vkDestroyImageView(g_Device, g_FontView, nullptr);
	if (g_FontImage)
		vkDestroyImage(g_Device, g_FontImage, nullptr);
	if (g_FontMemory)
		vkFreeMemory(g_Device, g_FontMemory, nullptr);
	if (g_FontSampler)
		vkDestroySampler(g_Device, g_FontSampler, nullptr);

	if (g_DescriptorSetLayout)
		vkDestroyDescriptorSetLayout(g_Device, g_DescriptorSetLayout, nullptr);
	if (g_PipelineLayout)
		vkDestroyPipelineLayout(g_Device, g_PipelineLayout, nullptr);
	if (g_Pipeline)
		vkDestroyPipeline(g_Device, g_Pipeline, nullptr);
}


void ImGui_ImplGlfwVulkan_Shutdown()
{
	ImGui_ImplGlfwVulkan_InvalidateDeviceObjects();
	ImGui::Shutdown();
}

void ImGui_ImplGlfwVulkan_SetMousePos(const uint32_t& mouseposx, const uint32_t& mouseposy)
{
	g_MousePosX = mouseposx;
	g_MousePosY = mouseposy;
}

void ImGui_ImplGlfwVulkan_MousePressed(bool bPressed)
{
	g_bMousePressed = bPressed;
}

void ImGui_ImplGlfwVulkan_MouseOnWinodws(bool bOnWindows)
{
	g_bMouseOnWindows = bOnWindows;
}