/*
*	Copyright 2015-2016 Tomas Mikalauskas. All rights reserved.
*	GitHub repository - https://github.com/TywyllSoftware/TywRenderer
*	This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <RendererPch\stdafx.h>

//Triangle Includes
#include "Main.h"
#include <conio.h>

//Renderer Includes
#include <Renderer\VKRenderer.h>
#include <Renderer\Vulkan\VulkanTools.h>
#include <Renderer\Vulkan\VulkanTextureLoader.h>
#include <Renderer\Geometry\VertData.h>


//math
#include <External\glm\glm\gtc\matrix_inverse.hpp>

//Global variables
//====================================================================================

uint32_t	g_iDesktopWidth = 0;
uint32_t	g_iDesktopHeight = 0;
bool		g_bPrepared = false;

glm::vec3	g_Rotation = glm::vec3();
glm::vec3	g_CameraPos = glm::vec3();
glm::vec2	g_MousePos;


// Use to adjust mouse rotation speed
float		g_RotationSpeed = 1.0f;
// Use to adjust mouse zoom speed
float		g_ZoomSpeed = 1.0f;
float       g_zoom = 1.0f;

VkClearColorValue g_DefaultClearColor = { { 0.5f, 0.5f, 0.5f, 1.0f } };


#define VERTEX_BUFFER_BIND_ID 0
// Set to "true" to enable Vulkan's validation layers
// See vulkandebug.cpp for details
#define ENABLE_VALIDATION false
// Set to "true" to use staging buffers for uploading
// vertex and index data to device local memory
// See "prepareVertices" for details on what's staging
// and on why to use it
#define USE_STAGING true

#define GAMEPAD_BUTTON_A 0x1000
#define GAMEPAD_BUTTON_B 0x1001
#define GAMEPAD_BUTTON_X 0x1002
#define GAMEPAD_BUTTON_Y 0x1003
#define GAMEPAD_BUTTON_L1 0x1004
#define GAMEPAD_BUTTON_R1 0x1005
#define GAMEPAD_BUTTON_START 0x1006


//====================================================================================


//functions
//====================================================================================

bool GenerateEvents(MSG& msg);
void WIN_Sizing(WORD side, RECT *rect);
LRESULT CALLBACK HandleWindowMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

//====================================================================================



class Renderer final: public VKRenderer
{
private:
	// The pipeline (state objects) is a static store for the 3D pipeline states (including shaders)
	// Other than OpenGL this makes you setup the render states up-front
	// If different render states are required you need to setup multiple pipelines
	// and switch between them
	// Note that there are a few dynamic states (scissor, viewport, line width) that
	// can be set from a command buffer and does not have to be part of the pipeline
	// This basic example only uses one pipeline
	VkPipeline pipeline;

	// The pipeline layout defines the resource binding slots to be used with a pipeline
	// This includes bindings for buffes (ubos, ssbos), images and sampler
	// A pipeline layout can be used for multiple pipeline (state objects) as long as 
	// their shaders use the same binding layout
	VkPipelineLayout pipelineLayout;

	// The descriptor set stores the resources bound to the binding points in a shader
	// It connects the binding points of the different shaders with the buffers and images
	// used for those bindings
	VkDescriptorSet descriptorSet;

	// The descriptor set layout describes the shader binding points without referencing
	// the actual buffers. 
	// Like the pipeline layout it's pretty much a blueprint and can be used with
	// different descriptor sets as long as the binding points (and shaders) match
	VkDescriptorSetLayout descriptorSetLayout;

	// Synchronization semaphores
	// Semaphores are used to synchronize dependencies between command buffers
	// We use them to ensure that we e.g. don't present to the swap chain
	// until all rendering has completed
	struct
	{
		// Swap chain image presentation
		VkSemaphore presentComplete;
		// Command buffer submission and execution
		VkSemaphore renderComplete;
		// Text overlay submission and execution
		VkSemaphore textOverlayComplete;
	} Semaphores;


	VkTools::VulkanTexture m_NormalTexture;
	VkTools::VulkanTexture m_DiffuseTexture;

	struct {
		glm::mat4 projectionMatrix;
		glm::mat4 modelMatrix;
		glm::mat4 viewMatrix;
		glm::mat4 normal;
		glm::vec4 viewPos;
		float lodBias = 0.0f;
	}m_uboVS;

	struct {
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkDescriptorBufferInfo descriptor;
	}  uniformDataVS;

	struct {
		VkBuffer buf;
		VkDeviceMemory mem;
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		int count;
		VkBuffer buf;
		VkDeviceMemory mem;
	} indices;

public:
	Renderer();
	~Renderer();

	void BuildCommandBuffers() override;
	void UpdateUniformBuffers() override;
	void PrepareUniformBuffers() override;
	void PrepareVertices(bool useStagingBuffers) override;
	void VLoadTexture(std::string fileName, VkFormat format, bool forceLinearTiling, bool bUseCubeMap = false) override;
	void PreparePipeline() override;
	void SetupDescriptorSet() override;
	void SetupDescriptorSetLayout() override;
	void SetupDescriptorPool() override;
	void LoadAssets() override;
	void StartFrame() override;
	void PrepareSemaphore() override;

	void ChangeLodBias(float delta);
};


Renderer::Renderer()
{

}

Renderer::~Renderer()
{
	//Destroy texture
	vkDestroyImageView(m_pWRenderer->m_SwapChain.device, m_DiffuseTexture.view, nullptr);
	vkDestroyImage(m_pWRenderer->m_SwapChain.device, m_DiffuseTexture.image, nullptr);
	vkFreeMemory(m_pWRenderer->m_SwapChain.device, m_DiffuseTexture.deviceMemory, nullptr);
	vkDestroySampler(m_pWRenderer->m_SwapChain.device, m_DiffuseTexture.sampler, nullptr);

	vkDestroyImageView(m_pWRenderer->m_SwapChain.device, m_NormalTexture.view, nullptr);
	vkDestroyImage(m_pWRenderer->m_SwapChain.device, m_NormalTexture.image, nullptr);
	vkFreeMemory(m_pWRenderer->m_SwapChain.device, m_NormalTexture.deviceMemory, nullptr);
	vkDestroySampler(m_pWRenderer->m_SwapChain.device, m_NormalTexture.sampler, nullptr);

	//Destroy Shader Module
	for (int i = 0; i < m_ShaderModules.size(); i++)
	{
		vkDestroyShaderModule(m_pWRenderer->m_SwapChain.device, m_ShaderModules[i], nullptr);
	}

	//Release semaphores
	vkDestroySemaphore(m_pWRenderer->m_SwapChain.device, Semaphores.presentComplete, nullptr);
	vkDestroySemaphore(m_pWRenderer->m_SwapChain.device, Semaphores.renderComplete, nullptr);
	vkDestroySemaphore(m_pWRenderer->m_SwapChain.device, Semaphores.textOverlayComplete, nullptr);

	//DestroyPipeline
	vkDestroyPipeline(m_pWRenderer->m_SwapChain.device, pipeline, nullptr);
	vkDestroyPipelineLayout(m_pWRenderer->m_SwapChain.device, pipelineLayout, nullptr);

	vkDestroyDescriptorSetLayout(m_pWRenderer->m_SwapChain.device, descriptorSetLayout, nullptr);

	//DestroyBuffer
	vkDestroyBuffer(m_pWRenderer->m_SwapChain.device, uniformDataVS.buffer, nullptr);
	vkDestroyBuffer(m_pWRenderer->m_SwapChain.device, indices.buf, nullptr);
	vkDestroyBuffer(m_pWRenderer->m_SwapChain.device, vertices.buf, nullptr);

	//Free memory
	vkFreeMemory(m_pWRenderer->m_SwapChain.device, uniformDataVS.memory, nullptr);
	vkFreeMemory(m_pWRenderer->m_SwapChain.device, indices.mem, nullptr);
	vkFreeMemory(m_pWRenderer->m_SwapChain.device, vertices.mem, nullptr);
}

void Renderer::PrepareSemaphore()
{
	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = NULL;

	// This semaphore ensures that the image is complete
	// before starting to submit again
	VK_CHECK_RESULT(vkCreateSemaphore(m_pWRenderer->m_SwapChain.device, &semaphoreCreateInfo, nullptr, &Semaphores.presentComplete));

	// This semaphore ensures that all commands submitted
	// have been finished before submitting the image to the queue
	VK_CHECK_RESULT(vkCreateSemaphore(m_pWRenderer->m_SwapChain.device, &semaphoreCreateInfo, nullptr, &Semaphores.renderComplete));


	// This semaphore ensures that all commands submitted
	// have been finished before submitting the image to the queue
	VK_CHECK_RESULT(vkCreateSemaphore(m_pWRenderer->m_SwapChain.device, &semaphoreCreateInfo, nullptr, &Semaphores.textOverlayComplete));
}


void Renderer::ChangeLodBias(float delta)
{
	m_uboVS.lodBias += delta;
	if (m_uboVS.lodBias < 0.0f)
	{
		m_uboVS.lodBias = 0.0f;
	}
	if (m_uboVS.lodBias > m_NormalTexture.mipLevels)
	{
		m_uboVS.lodBias = m_NormalTexture.mipLevels;
	}

	UpdateUniformBuffers();
}


void Renderer::BuildCommandBuffers()
{
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.pNext = NULL;

	// Set clear values for all framebuffer attachments with loadOp set to clear
	// We use two attachments (color and depth) that are cleared at the 
	// start of the subpass and as such we need to set clear values for both
	VkClearValue clearValues[2];
	clearValues[0].color = g_DefaultClearColor;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = NULL;
	renderPassBeginInfo.renderPass = m_pWRenderer->m_RenderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = g_iDesktopWidth;
	renderPassBeginInfo.renderArea.extent.height = g_iDesktopHeight;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;

	for (int32_t i = 0; i < m_pWRenderer->m_DrawCmdBuffers.size(); ++i)
	{
		// Set target frame buffer
		renderPassBeginInfo.framebuffer = m_pWRenderer->m_FrameBuffers[i];

		VK_CHECK_RESULT(vkBeginCommandBuffer(m_pWRenderer->m_DrawCmdBuffers[i], &cmdBufInfo));

		// Start the first sub pass specified in our default render pass setup by the base class
		// This will clear the color and depth attachment
		vkCmdBeginRenderPass(m_pWRenderer->m_DrawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Update dynamic viewport state
		VkViewport viewport = {};
		viewport.height = (float)g_iDesktopHeight;
		viewport.width = (float)g_iDesktopWidth;
		viewport.minDepth = (float) 0.0f;
		viewport.maxDepth = (float) 1.0f;
		vkCmdSetViewport(m_pWRenderer->m_DrawCmdBuffers[i], 0, 1, &viewport);

		// Update dynamic scissor state
		VkRect2D scissor = {};
		scissor.extent.width = g_iDesktopWidth;
		scissor.extent.height = g_iDesktopHeight;
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		vkCmdSetScissor(m_pWRenderer->m_DrawCmdBuffers[i], 0, 1, &scissor);

		// Bind descriptor sets describing shader binding points
		vkCmdBindDescriptorSets(m_pWRenderer->m_DrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

		// Bind the rendering pipeline
		// The pipeline (state object) contains all states of the rendering pipeline
		// So once we bind a pipeline all states that were set upon creation of that
		// pipeline will be set
		vkCmdBindPipeline(m_pWRenderer->m_DrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// Bind triangle vertex buffer (contains position and colors)
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(m_pWRenderer->m_DrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &vertices.buf, offsets);

		// Bind triangle index buffer
		vkCmdBindIndexBuffer(m_pWRenderer->m_DrawCmdBuffers[i], indices.buf, 0, VK_INDEX_TYPE_UINT32);

		// Draw indexed triangle
		vkCmdDrawIndexed(m_pWRenderer->m_DrawCmdBuffers[i], indices.count, 1, 0, 0, 1);

		vkCmdEndRenderPass(m_pWRenderer->m_DrawCmdBuffers[i]);

		// Add a present memory barrier to the end of the command buffer
		// This will transform the frame buffer color attachment to a
		// new layout for presenting it to the windowing system integration 
		VkImageMemoryBarrier prePresentBarrier = {};
		prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		prePresentBarrier.pNext = NULL;
		prePresentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		prePresentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		prePresentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		prePresentBarrier.image = m_pWRenderer->m_SwapChain.buffers[i].image;

		VkImageMemoryBarrier *pMemoryBarrier = &prePresentBarrier;
		vkCmdPipelineBarrier(
			m_pWRenderer->m_DrawCmdBuffers[i],
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VK_FLAGS_NONE,
			0, nullptr,
			0, nullptr,
			1, &prePresentBarrier);

		VK_CHECK_RESULT(vkEndCommandBuffer(m_pWRenderer->m_DrawCmdBuffers[i]));
	}
}

void Renderer::UpdateUniformBuffers()
{
	// Update matrices
	m_uboVS.projectionMatrix = glm::perspective(glm::radians(60.0f), (float)g_iDesktopWidth / (float)g_iDesktopHeight, 0.1f, 256.0f);

	m_uboVS.viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, g_zoom));

	m_uboVS.modelMatrix = m_uboVS.viewMatrix * glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, 5.0f));
	m_uboVS.modelMatrix = glm::rotate(m_uboVS.modelMatrix, glm::radians(g_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	m_uboVS.modelMatrix = glm::rotate(m_uboVS.modelMatrix, glm::radians(g_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	m_uboVS.modelMatrix = glm::rotate(m_uboVS.modelMatrix, glm::radians(g_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
	m_uboVS.normal = glm::inverseTranspose(m_uboVS.modelMatrix);


	m_uboVS.viewPos = glm::vec4(0.0f, 0.0f, -5.0f, 0.0f);

	// Map uniform buffer and update it
	uint8_t *pData;
	VK_CHECK_RESULT(vkMapMemory(m_pWRenderer->m_SwapChain.device, uniformDataVS.memory, 0, sizeof(m_uboVS), 0, (void **)&pData));
	memcpy(pData, &m_uboVS, sizeof(m_uboVS));
	vkUnmapMemory(m_pWRenderer->m_SwapChain.device, uniformDataVS.memory);
}

void Renderer::PrepareUniformBuffers()
{
	// Prepare and initialize a uniform buffer block containing shader uniforms
	// In Vulkan there are no more single uniforms like in GL
	// All shader uniforms are passed as uniform buffer blocks 
	VkMemoryRequirements memReqs;

	// Vertex shader uniform buffer block
	VkBufferCreateInfo bufferInfo = {};
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.pNext = NULL;
	allocInfo.allocationSize = 0;
	allocInfo.memoryTypeIndex = 0;

	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeof(m_uboVS);
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	// Create a new buffer
	VK_CHECK_RESULT(vkCreateBuffer(m_pWRenderer->m_SwapChain.device, &bufferInfo, nullptr, &uniformDataVS.buffer));
	// Get memory requirements including size, alignment and memory type 
	vkGetBufferMemoryRequirements(m_pWRenderer->m_SwapChain.device, uniformDataVS.buffer, &memReqs);
	allocInfo.allocationSize = memReqs.size;
	// Get the memory type index that supports host visibile memory access
	// Most implementations offer multiple memory tpyes and selecting the 
	// correct one to allocate memory from is important
	// We also want the buffer to be host coherent so we don't have to flush 
	// after every update. 
	// Note that this may affect performance so you might not want to do this 
	// in a real world application that updates buffers on a regular base
	allocInfo.memoryTypeIndex = VkTools::GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_pWRenderer->m_DeviceMemoryProperties);
	// Allocate memory for the uniform buffer
	VK_CHECK_RESULT(vkAllocateMemory(m_pWRenderer->m_SwapChain.device, &allocInfo, nullptr, &(uniformDataVS.memory)));
	// Bind memory to buffer
	VK_CHECK_RESULT(vkBindBufferMemory(m_pWRenderer->m_SwapChain.device, uniformDataVS.buffer, uniformDataVS.memory, 0));

	// Store information in the uniform's descriptor
	uniformDataVS.descriptor.buffer = uniformDataVS.buffer;
	uniformDataVS.descriptor.offset = 0;
	uniformDataVS.descriptor.range = sizeof(m_uboVS);

	UpdateUniformBuffers();
}

void Renderer::PrepareVertices(bool useStagingBuffers)
{
	struct Vertex {
		float pos[3];
		float uv[2];
		float normal[3];
	};

#define DIM 1.0f
#define NORMAL { 0.0f, 0.0f, 1.0f }
	std::vector<drawVert> vertData =
	{
		{ glm::vec3(DIM,DIM,    0),	   glm::vec3(0,0,1),  glm::vec3(0,0,0), glm::vec3(0,0,0),  glm::vec2(1,1) },
		{ glm::vec3(-DIM,DIM,  0),     glm::vec3(0,0,1),  glm::vec3(0,0,0), glm::vec3(0,0,0),  glm::vec2(0,1) },
		{ glm::vec3(-DIM, -DIM,  0),   glm::vec3(0,0,1),  glm::vec3(0,0,0), glm::vec3(0,0,0),  glm::vec2(0,0) },
		{ glm::vec3(DIM,-DIM,  0),     glm::vec3(0,0,1),  glm::vec3(0,0,0), glm::vec3(0,0,0),  glm::vec2(1,0) },
	};

	// Setup indices
	std::vector<uint32_t> indexBuffer = { 0, 1, 2, 2,3,0 };

	//We have 2 triangles
	for (int i = 0; i < indexBuffer.size();  i+=3)
	{
		uint32_t i1 = indexBuffer[i];
		uint32_t i2 = indexBuffer[i + 1];
		uint32_t i3 = indexBuffer[i + 2];

		const glm::vec3& v1 = vertData[i1].vertex;
		const glm::vec3& v2 = vertData[i2].vertex;
		const glm::vec3& v3 = vertData[i3].vertex;


		const glm::vec2& uv1 = vertData[i1].tex;
		const glm::vec2& uv2 = vertData[i2].tex;
		const glm::vec2& uv3 = vertData[i3].tex;

		//calculate edges
		glm::vec3 Edge1 = v2 - v1;
		glm::vec3 Edge2 = v3 - v1;

		//calculate uv edges
		glm::vec2 Edge1Uv = uv2 - uv1;
		glm::vec2 Edge2Uv = uv3 - uv1;


		float r = 1.0f / (Edge1Uv.x * Edge2Uv.y - Edge1Uv.y * Edge2Uv.x);
		glm::vec3 tangent = (Edge1 * Edge2Uv.y - Edge2 * Edge1Uv.y)*r;
		glm::vec3 bitangent = (Edge2 * Edge1Uv.x - Edge1 * Edge2Uv.x)*r;

		vertData[i1].tangent = tangent;
		vertData[i2].tangent = tangent;
		vertData[i3].tangent = tangent;

		vertData[i1].bitangent = bitangent;
		vertData[i2].bitangent = bitangent;
		vertData[i3].bitangent = bitangent;
	}

	// See "Going Further"
	for (unsigned int i = 0; i < 4; i += 1)
	{
		glm::vec3 & n = vertData[i].normal;
		glm::vec3 & t = vertData[i].tangent;
		glm::vec3 & b = vertData[i].bitangent;

		// Gram-Schmidt orthogonalize
		t = glm::normalize(t - n * glm::dot(n, t));

		// Calculate handedness
		if (glm::dot(glm::cross(n, t), b) < 0.0f) {
			t = t * -1.0f;
		}

	}





	indices.count = static_cast<uint32_t>(indexBuffer.size());
	uint32_t indexBufferSize = indices.count * sizeof(uint32_t);

	VkMemoryAllocateInfo memAlloc = {};
	memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	VkMemoryRequirements memReqs;

	void *data;

	if (useStagingBuffers)
	{
		// Static data like vertex and index buffer should be stored on the device memory 
		// for optimal (and fastest) access by the GPU
		//
		// To achieve this we use so-called "staging buffers" :
		// - Create a buffer that's visible to the host (and can be mapped)
		// - Copy the data to this buffer
		// - Create another buffer that's local on the device (VRAM) with the same size
		// - Copy the data from the host to the device using a command buffer
		// - Delete the host visible (staging) buffer
		// - Use the device local buffers for rendering

		struct StagingBuffer {
			VkDeviceMemory memory;
			VkBuffer buffer;
		};

		struct {
			StagingBuffer vertices;
			StagingBuffer indices;
		} stagingBuffers;

		// Vertex buffer
		VkBufferCreateInfo vertexBufferInfo = {};
		vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		vertexBufferInfo.size = sizeof(drawVert) * 4;
		// Buffer is used as the copy source
		vertexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		// Create a host-visible buffer to copy the vertex data to (staging buffer)
		VK_CHECK_RESULT(vkCreateBuffer(m_pWRenderer->m_SwapChain.device, &vertexBufferInfo, nullptr, &stagingBuffers.vertices.buffer));
		vkGetBufferMemoryRequirements(m_pWRenderer->m_SwapChain.device, stagingBuffers.vertices.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = VkTools::GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, m_pWRenderer->m_DeviceMemoryProperties);
		VK_CHECK_RESULT(vkAllocateMemory(m_pWRenderer->m_SwapChain.device, &memAlloc, nullptr, &stagingBuffers.vertices.memory));
		// Map and copy
		VK_CHECK_RESULT(vkMapMemory(m_pWRenderer->m_SwapChain.device, stagingBuffers.vertices.memory, 0, memAlloc.allocationSize, 0, &data));
		memcpy(data, vertData.data(), sizeof(drawVert) * vertData.size());
		vkUnmapMemory(m_pWRenderer->m_SwapChain.device, stagingBuffers.vertices.memory);
		VK_CHECK_RESULT(vkBindBufferMemory(m_pWRenderer->m_SwapChain.device, stagingBuffers.vertices.buffer, stagingBuffers.vertices.memory, 0));

		// Create the destination buffer with device only visibility
		// Buffer will be used as a vertex buffer and is the copy destination
		vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VK_CHECK_RESULT(vkCreateBuffer(m_pWRenderer->m_SwapChain.device, &vertexBufferInfo, nullptr, &vertices.buf));
		vkGetBufferMemoryRequirements(m_pWRenderer->m_SwapChain.device, vertices.buf, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = VkTools::GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_pWRenderer->m_DeviceMemoryProperties);
		VK_CHECK_RESULT(vkAllocateMemory(m_pWRenderer->m_SwapChain.device, &memAlloc, nullptr, &vertices.mem));
		VK_CHECK_RESULT(vkBindBufferMemory(m_pWRenderer->m_SwapChain.device, vertices.buf, vertices.mem, 0));

		// Index buffer
		VkBufferCreateInfo indexbufferInfo = {};
		indexbufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		indexbufferInfo.size = indexBufferSize;
		indexbufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		// Copy index data to a buffer visible to the host (staging buffer)
		VK_CHECK_RESULT(vkCreateBuffer(m_pWRenderer->m_SwapChain.device, &indexbufferInfo, nullptr, &stagingBuffers.indices.buffer));
		vkGetBufferMemoryRequirements(m_pWRenderer->m_SwapChain.device, stagingBuffers.indices.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = VkTools::GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, m_pWRenderer->m_DeviceMemoryProperties);
		VK_CHECK_RESULT(vkAllocateMemory(m_pWRenderer->m_SwapChain.device, &memAlloc, nullptr, &stagingBuffers.indices.memory));
		VK_CHECK_RESULT(vkMapMemory(m_pWRenderer->m_SwapChain.device, stagingBuffers.indices.memory, 0, indexBufferSize, 0, &data));
		memcpy(data, indexBuffer.data(), indexBufferSize);
		vkUnmapMemory(m_pWRenderer->m_SwapChain.device, stagingBuffers.indices.memory);
		VK_CHECK_RESULT(vkBindBufferMemory(m_pWRenderer->m_SwapChain.device, stagingBuffers.indices.buffer, stagingBuffers.indices.memory, 0));

		// Create destination buffer with device only visibility
		indexbufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VK_CHECK_RESULT(vkCreateBuffer(m_pWRenderer->m_SwapChain.device, &indexbufferInfo, nullptr, &indices.buf));
		vkGetBufferMemoryRequirements(m_pWRenderer->m_SwapChain.device, indices.buf, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = VkTools::GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_pWRenderer->m_DeviceMemoryProperties);
		VK_CHECK_RESULT(vkAllocateMemory(m_pWRenderer->m_SwapChain.device, &memAlloc, nullptr, &indices.mem));
		VK_CHECK_RESULT(vkBindBufferMemory(m_pWRenderer->m_SwapChain.device, indices.buf, indices.mem, 0));

		VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
		cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmdBufferBeginInfo.pNext = NULL;

		// Buffer copies have to be submitted to a queue, so we need a command buffer for them
		// Note that some devices offer a dedicated transfer queue (with only the transfer bit set)
		// If you do lots of copies (especially at runtime) it's advised to use such a queu instead
		// of a generalized graphics queue (that also supports transfers)
		VkCommandBuffer copyCmd = GetCommandBuffer(true);

		// Put buffer region copies into command buffer
		// Note that the staging buffer must not be deleted before the copies have been submitted and executed

		VkBufferCopy copyRegion = {};

		// Vertex buffer
		copyRegion.size = sizeof(drawVert) * 4;
		vkCmdCopyBuffer(
			copyCmd,
			stagingBuffers.vertices.buffer,
			vertices.buf,
			1,
			&copyRegion);
		// Index buffer
		copyRegion.size = indexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			stagingBuffers.indices.buffer,
			indices.buf,
			1,
			&copyRegion);

		FlushCommandBuffer(copyCmd);

		// Destroy staging buffers
		vkDestroyBuffer(m_pWRenderer->m_SwapChain.device, stagingBuffers.vertices.buffer, nullptr);
		vkFreeMemory(m_pWRenderer->m_SwapChain.device, stagingBuffers.vertices.memory, nullptr);
		vkDestroyBuffer(m_pWRenderer->m_SwapChain.device, stagingBuffers.indices.buffer, nullptr);
		vkFreeMemory(m_pWRenderer->m_SwapChain.device, stagingBuffers.indices.memory, nullptr);
	}
	else
	{
		// Don't use staging
		// Create host-visible buffers only and use these for rendering
		// This is not advised for real world applications and will
		// result in lower performances at least on devices that
		// separate between host visible and device local memory

		// Vertex buffer
		VkBufferCreateInfo vertexBufferInfo = {};
		vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		vertexBufferInfo.size = sizeof(drawVert) * 4;
		vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		// Copy vertex data to a buffer visible to the host
		VK_CHECK_RESULT(vkCreateBuffer(m_pWRenderer->m_SwapChain.device, &vertexBufferInfo, nullptr, &vertices.buf));
		vkGetBufferMemoryRequirements(m_pWRenderer->m_SwapChain.device, vertices.buf, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = VkTools::GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, m_pWRenderer->m_DeviceMemoryProperties);
		VK_CHECK_RESULT(vkAllocateMemory(m_pWRenderer->m_SwapChain.device, &memAlloc, nullptr, &vertices.mem));
		VK_CHECK_RESULT(vkMapMemory(m_pWRenderer->m_SwapChain.device, vertices.mem, 0, memAlloc.allocationSize, 0, &data));
		memcpy(data, vertData.data(), sizeof(drawVert) * vertData.size());
		vkUnmapMemory(m_pWRenderer->m_SwapChain.device, vertices.mem);
		VK_CHECK_RESULT(vkBindBufferMemory(m_pWRenderer->m_SwapChain.device, vertices.buf, vertices.mem, 0));

		// Index buffer
		VkBufferCreateInfo indexbufferInfo = {};
		indexbufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		indexbufferInfo.size = indexBufferSize;
		indexbufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

		// Copy index data to a buffer visible to the host
		memset(&indices, 0, sizeof(indices));
		VK_CHECK_RESULT(vkCreateBuffer(m_pWRenderer->m_SwapChain.device, &indexbufferInfo, nullptr, &indices.buf));
		vkGetBufferMemoryRequirements(m_pWRenderer->m_SwapChain.device, indices.buf, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = VkTools::GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, m_pWRenderer->m_DeviceMemoryProperties);
		VK_CHECK_RESULT(vkAllocateMemory(m_pWRenderer->m_SwapChain.device, &memAlloc, nullptr, &indices.mem));
		VK_CHECK_RESULT(vkMapMemory(m_pWRenderer->m_SwapChain.device, indices.mem, 0, indexBufferSize, 0, &data));
		memcpy(data, indexBuffer.data(), indexBufferSize);
		vkUnmapMemory(m_pWRenderer->m_SwapChain.device, indices.mem);
		VK_CHECK_RESULT(vkBindBufferMemory(m_pWRenderer->m_SwapChain.device, indices.buf, indices.mem, 0));
	}

	// Binding description
	vertices.bindingDescriptions.resize(1);
	vertices.bindingDescriptions[0].binding = VERTEX_BUFFER_BIND_ID;
	vertices.bindingDescriptions[0].stride = sizeof(drawVert);
	vertices.bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	// Attribute descriptions
	// Describes memory layout and shader attribute locations
	vertices.attributeDescriptions.resize(5);

	// Location 0 : Position
	vertices.attributeDescriptions[0].binding = VERTEX_BUFFER_BIND_ID;
	vertices.attributeDescriptions[0].location = 0;
	vertices.attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertices.attributeDescriptions[0].offset = offsetof(drawVert, vertex);
	// Location 1 : Uv
	vertices.attributeDescriptions[1].binding = VERTEX_BUFFER_BIND_ID;
	vertices.attributeDescriptions[1].location = 1;
	vertices.attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertices.attributeDescriptions[1].offset = offsetof(drawVert, tex);

	// Location 2 : Normals
	vertices.attributeDescriptions[2].binding = VERTEX_BUFFER_BIND_ID;
	vertices.attributeDescriptions[2].location = 2;
	vertices.attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertices.attributeDescriptions[2].offset = offsetof(drawVert, normal);

	// Location 3 : tangent
	vertices.attributeDescriptions[3].binding = VERTEX_BUFFER_BIND_ID;
	vertices.attributeDescriptions[3].location = 3;
	vertices.attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertices.attributeDescriptions[3].offset = offsetof(drawVert, tangent);


	// Location 4 : bitangent
	vertices.attributeDescriptions[4].binding = VERTEX_BUFFER_BIND_ID;
	vertices.attributeDescriptions[4].location = 4;
	vertices.attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertices.attributeDescriptions[4].offset = offsetof(drawVert, bitangent);


	// Assign to vertex input state
	vertices.inputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertices.inputState.pNext = NULL;
	vertices.inputState.flags = VK_FLAGS_NONE;
	vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
	vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
	vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
	vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
}


void Renderer::PreparePipeline()
{

	// Create our rendering pipeline used in this example
	// Vulkan uses the concept of rendering pipelines to encapsulate
	// fixed states
	// This replaces OpenGL's huge (and cumbersome) state machine
	// A pipeline is then stored and hashed on the GPU making
	// pipeline changes much faster than having to set dozens of 
	// states
	// In a real world application you'd have dozens of pipelines
	// for every shader set used in a scene
	// Note that there are a few states that are not stored with
	// the pipeline. These are called dynamic states and the 
	// pipeline only stores that they are used with this pipeline,
	// but not their states

	// Assign states
	// Assign pipeline state create information
	VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};

	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	// The layout used for this pipeline
	pipelineCreateInfo.layout = pipelineLayout;
	// Renderpass this pipeline is attached to
	pipelineCreateInfo.renderPass = m_pWRenderer->m_RenderPass;

	// Vertex input state
	// Describes the topoloy used with this pipeline
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
	inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	// This pipeline renders vertex data as triangle lists
	inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Rasterization state
	VkPipelineRasterizationStateCreateInfo rasterizationState = {};
	rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	// Solid polygon mode
	rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
	// No culling
	rasterizationState.cullMode = VK_CULL_MODE_NONE;
	rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationState.depthClampEnable = VK_FALSE;
	rasterizationState.rasterizerDiscardEnable = VK_FALSE;
	rasterizationState.depthBiasEnable = VK_FALSE;
	rasterizationState.lineWidth = 1.0f;

	// Color blend state
	// Describes blend modes and color masks
	VkPipelineColorBlendStateCreateInfo colorBlendState = {};
	colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	// One blend attachment state
	// Blending is not used in this example
	VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
	blendAttachmentState[0].colorWriteMask = 0xf;
	blendAttachmentState[0].blendEnable = VK_FALSE;
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = blendAttachmentState;

	// Viewport state
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	// One viewport
	viewportState.viewportCount = 1;
	// One scissor rectangle
	viewportState.scissorCount = 1;

	// Enable dynamic states
	// Describes the dynamic states to be used with this pipeline
	// Dynamic states can be set even after the pipeline has been created
	// So there is no need to create new pipelines just for changing
	// a viewport's dimensions or a scissor box
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	// The dynamic state properties themselves are stored in the command buffer
	std::vector<VkDynamicState> dynamicStateEnables;
	dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);
	dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.pDynamicStates = dynamicStateEnables.data();
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

	// Depth and stencil state
	// Describes depth and stenctil test and compare ops
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

	// Multi sampling state
	VkPipelineMultisampleStateCreateInfo multisampleState = {};
	multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleState.pSampleMask = NULL;
	// No multi sampling used in this example
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Load shaders
	//Shaders are loaded from the SPIR-V format, which can be generated from glsl
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
	shaderStages[0] = LoadShader(GetAssetPath() + "Shaders/NormalMapping/NormalMapping.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	shaderStages[1] = LoadShader(GetAssetPath() + "Shaders/NormalMapping/NormalMapping.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	// Create Pipeline state VI-IA-VS-VP-RS-FS-CB
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCreateInfo.pStages = shaderStages.data();
	pipelineCreateInfo.pVertexInputState = &vertices.inputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.renderPass = m_pWRenderer->m_RenderPass;
	pipelineCreateInfo.pDynamicState = &dynamicState;

	// Create rendering pipeline
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_pWRenderer->m_SwapChain.device, m_pWRenderer->m_PipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
}



void Renderer::VLoadTexture(std::string fileName, VkFormat format, bool forceLinearTiling, bool bUseCubeMap)
{
	
}


void Renderer::SetupDescriptorSetLayout()
{
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
	{
		// Binding 0 : Vertex shader uniform buffer
		VkTools::Initializer::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0),

		// Binding 1 : Fragment shader image sampler Normal Map
		VkTools::Initializer::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1),

		// Binding 2 : Fragment shader image sampler Diffuse Map
		VkTools::Initializer::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
	};

	VkDescriptorSetLayoutCreateInfo descriptorLayout = VkTools::Initializer::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_pWRenderer->m_SwapChain.device, &descriptorLayout, nullptr, &descriptorSetLayout));

	VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = VkTools::Initializer::PipelineLayoutCreateInfo(&descriptorSetLayout, 1);
	VK_CHECK_RESULT(vkCreatePipelineLayout(m_pWRenderer->m_SwapChain.device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
}

void Renderer::SetupDescriptorPool()
{
	// Example uses one ubo and one image sampler
	std::vector<VkDescriptorPoolSize> poolSizes =
	{
		VkTools::Initializer::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
		VkTools::Initializer::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)
	};

	VkDescriptorPoolCreateInfo descriptorPoolInfo = VkTools::Initializer::DescriptorPoolCreateInfo(poolSizes.size(),poolSizes.data(),2);
	VK_CHECK_RESULT(vkCreateDescriptorPool(m_pWRenderer->m_SwapChain.device, &descriptorPoolInfo, nullptr, &m_pWRenderer->m_DescriptorPool));
}


void Renderer::SetupDescriptorSet()
{
	VkDescriptorSetAllocateInfo allocInfo = VkTools::Initializer::DescriptorSetAllocateInfo(m_pWRenderer->m_DescriptorPool,&descriptorSetLayout,1);

	VK_CHECK_RESULT(vkAllocateDescriptorSets(m_pWRenderer->m_SwapChain.device, &allocInfo, &descriptorSet));

	// Image descriptor for the normalMap
	VkDescriptorImageInfo normalTexDescriptor = VkTools::Initializer::DescriptorImageInfo(m_NormalTexture.sampler, m_NormalTexture.view,VK_IMAGE_LAYOUT_GENERAL);

	//Image descriptor for the diffuseMap
	VkDescriptorImageInfo diffuseTexDescriptor = VkTools::Initializer::DescriptorImageInfo(m_DiffuseTexture.sampler, m_DiffuseTexture.view, VK_IMAGE_LAYOUT_GENERAL);

	std::vector<VkWriteDescriptorSet> writeDescriptorSets =
	{
		// Binding 0 : Vertex shader uniform buffer
		VkTools::Initializer::WriteDescriptorSet(descriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,	0,&uniformDataVS.descriptor),
	
		// Binding 1 : Fragment shader texture sampler Normal Map
		VkTools::Initializer::WriteDescriptorSet(descriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,&normalTexDescriptor),

		// Binding 2 : Fragment shader texture sampler Diffuse Map
		VkTools::Initializer::WriteDescriptorSet(descriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2,&diffuseTexDescriptor)
	};

	vkUpdateDescriptorSets(m_pWRenderer->m_SwapChain.device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
}


void Renderer::StartFrame()
{
	{
		// Get next image in the swap chain (back/front buffer)
		VK_CHECK_RESULT(m_pWRenderer->m_SwapChain.GetNextImage(Semaphores.presentComplete, &m_pWRenderer->m_currentBuffer));

		// Submit the post present image barrier to transform the image back to a color attachment
		// that can be used to write to by our render pass
		VkSubmitInfo submitInfo = VkTools::Initializer::SubmitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_pWRenderer->m_PostPresentCmdBuffers[m_pWRenderer->m_currentBuffer];

		VK_CHECK_RESULT(vkQueueSubmit(m_pWRenderer->m_Queue, 1, &submitInfo, VK_NULL_HANDLE));

		// Make sure that the image barrier command submitted to the queue 
		// has finished executing
		VK_CHECK_RESULT(vkQueueWaitIdle(m_pWRenderer->m_Queue));
	}



	{
		// The submit infor strcuture contains a list of
		// command buffers and semaphores to be submitted to a queue
		// If you want to submit multiple command buffers, pass an array
		VkPipelineStageFlags pipelineStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		VkSubmitInfo submitInfo = VkTools::Initializer::SubmitInfo();
		submitInfo.pWaitDstStageMask = &pipelineStages;
		// The wait semaphore ensures that the image is presented 
		// before we start submitting command buffers agein
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &Semaphores.presentComplete;
		// Submit the currently active command buffer
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_pWRenderer->m_DrawCmdBuffers[m_pWRenderer->m_currentBuffer];
		// The signal semaphore is used during queue presentation
		// to ensure that the image is not rendered before all
		// commands have been submitted
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &Semaphores.renderComplete;

		// Submit to the graphics queue
		VK_CHECK_RESULT(vkQueueSubmit(m_pWRenderer->m_Queue, 1, &submitInfo, VK_NULL_HANDLE));

		// Present the current buffer to the swap chain
		// We pass the signal semaphore from the submit info
		// to ensure that the image is not rendered until
		// all commands have been submitted
		VK_CHECK_RESULT(m_pWRenderer->m_SwapChain.QueuePresent(m_pWRenderer->m_Queue, m_pWRenderer->m_currentBuffer, Semaphores.renderComplete));
	}
}

void Renderer::LoadAssets()
{
	std::string fileName = (GetAssetPath() + "Textures/photosculpt/photosculpt-squarebricks-normal_bc5.KTX");
	if (!m_pTextureLoader->LoadTexture(fileName, VK_FORMAT_BC5_UNORM_BLOCK, &m_NormalTexture)) {
		printf("Could not load %s \n", fileName.c_str());
	}

	fileName = (GetAssetPath() + "Textures/photosculpt/photosculpt-squarebricks-diffuse_bc7.KTX");
	if (!m_pTextureLoader->LoadTexture(fileName, VK_FORMAT_BC7_UNORM_BLOCK, &m_DiffuseTexture)) {
		printf("Could not load %s \n", fileName.c_str());
	}
}


//Main global Renderer
Renderer g_Renderer;

int main()
{
	g_bPrepared = g_Renderer.VInitRenderer(800, 600, false, HandleWindowMessages);
	
#if defined(_WIN32)
	MSG msg;
#endif

	while (TRUE)
	{
		if (!GenerateEvents(msg))break;

		auto tStart = std::chrono::high_resolution_clock::now();
		g_Renderer.StartFrame();
		//Do something
		g_Renderer.EndFrame(nullptr);
		auto tEnd = std::chrono::high_resolution_clock::now();
	}
	g_Renderer.VShutdown();
	return 0;
}


LRESULT CALLBACK HandleWindowMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CLOSE:
		//prepared = false;
		DestroyWindow(hWnd);
		PostQuitMessage(0);
		break;
	case WM_PAINT:
		//ValidateRect(window, NULL);
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case 0x50:
			//paused = !paused;
			break;
		case VK_F1:
			//if (enableTextOverlay)
		{
			//textOverlay->visible = !textOverlay->visible;
		}
		break;
		case VK_ESCAPE:
			PostQuitMessage(0);
			break;
		}



		switch ((uint32_t)wParam)
		{
		case 0x6B:
		case GAMEPAD_BUTTON_R1:
			g_Renderer.ChangeLodBias(0.1f);
			break;
		case 0x6D:
		case GAMEPAD_BUTTON_L1:
			g_Renderer.ChangeLodBias(-0.1f);
			break;
		}


		/*
		if (camera.firstperson)
		{
		switch (wParam)
		{
		case 0x57:
		camera.keys.up = true;
		break;
		case 0x53:
		camera.keys.down = true;
		break;
		case 0x41:
		camera.keys.left = true;
		break;
		case 0x44:
		camera.keys.right = true;
		break;
		}
		}
		*/

		//keyPressed((uint32_t)wParam);
		break;
	case WM_KEYUP:
		/*
		if (camera.firstperson)
		{
		switch (wParam)
		{
		case 0x57:
		camera.keys.up = false;
		break;
		case 0x53:
		camera.keys.down = false;
		break;
		case 0x41:
		camera.keys.left = false;
		break;
		case 0x44:
		camera.keys.right = false;
		break;
		}
		}
		*/
		break;
	case WM_RBUTTONDOWN:
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
		g_MousePos.x = (float)LOWORD(lParam);
		g_MousePos.y = (float)HIWORD(lParam);
		break;
	case WM_MOUSEWHEEL:
	{
		short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		g_zoom += (float)wheelDelta * 0.005f * g_ZoomSpeed;
		//camera.translate(glm::vec3(0.0f, 0.0f, (float)wheelDelta * 0.005f * zoomSpeed));
		g_Renderer.UpdateUniformBuffers();
		break;
	}
	case WM_MOUSEMOVE:
		if (wParam & MK_RBUTTON)
		{
			int32_t posx = LOWORD(lParam);
			int32_t posy = HIWORD(lParam);
			g_zoom += (g_MousePos.y - (float)posy) * .005f * g_ZoomSpeed;
			//camera.translate(glm::vec3(-0.0f, 0.0f, (mousePos.y - (float)posy) * .005f * zoomSpeed));
			g_MousePos = glm::vec2((float)posx, (float)posy);
			g_Renderer.UpdateUniformBuffers();
		}
		if (wParam & MK_LBUTTON)
		{
			int32_t posx = LOWORD(lParam);
			int32_t posy = HIWORD(lParam);
			g_Rotation.x += (g_MousePos.y - (float)posy) * 1.25f * g_RotationSpeed;
			g_Rotation.y -= (g_MousePos.x - (float)posx) * 1.25f * g_RotationSpeed;
			//camera.rotate(glm::vec3((mousePos.y - (float)posy), -(mousePos.x - (float)posx), 0.0f));
			g_MousePos = glm::vec2((float)posx, (float)posy);
			g_Renderer.UpdateUniformBuffers();
		}
		if (wParam & MK_MBUTTON)
		{
			int32_t posx = LOWORD(lParam);
			int32_t posy = HIWORD(lParam);
			g_CameraPos.x -= (g_MousePos.x - (float)posx) * 0.01f;
			g_CameraPos.y -= (g_MousePos.y - (float)posy) * 0.01f;
			//camera.translate(glm::vec3(-(mousePos.x - (float)posx) * 0.01f, -(mousePos.y - (float)posy) * 0.01f, 0.0f));
			//viewChanged();
			g_MousePos.x = (float)posx;
			g_MousePos.y = (float)posy;
		}
		break;
	case WM_SIZE:
		RECT rect;
		if (GetWindowRect(hWnd, &rect))
		{
			WIN_Sizing(wParam, &rect);
		}
		break;
	case WM_EXITSIZEMOVE:
		if (g_bPrepared) {
			g_Renderer.VWindowResize(g_iDesktopHeight, g_iDesktopWidth);
			
		}
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


bool GenerateEvents(MSG& msg)
{
	if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
		{
			return false;
		}
		else
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return true;
}


void WIN_Sizing(WORD side, RECT *rect)
{
	// restrict to a standard aspect ratio
	g_iDesktopWidth = rect->right - rect->left;
	g_iDesktopHeight = rect->bottom - rect->top;

	// Adjust width/height for window decoration
	RECT decoRect = { 0, 0, 0, 0 };
	AdjustWindowRect(&decoRect, WINDOW_STYLE | WS_SYSMENU, FALSE);
	uint32_t decoWidth = decoRect.right - decoRect.left;
	uint32_t decoHeight = decoRect.bottom - decoRect.top;

	g_iDesktopWidth -= decoWidth;
	g_iDesktopHeight -= decoHeight;
}