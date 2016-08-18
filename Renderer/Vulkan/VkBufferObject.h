#pragma once
#include <External\vulkan\vulkan.h>

//forward declaration
class VulkanSwapChain;
class VulkanRendererInitializer;

struct TYWRENDERER_API VkBufferObject_s
{
	VkBufferObject_s(): buffer(0), memory(0){}
	VkBuffer& operator ()() { return buffer; }

	VkBuffer buffer;
	VkDeviceMemory memory;

	VkPipelineVertexInputStateCreateInfo inputState;
	std::vector<VkVertexInputBindingDescription> bindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
};


class TYWRENDERER_API VkBufferObject
{
public:
	VkBufferObject();
	static VkResult CreateBuffer(const VulkanSwapChain& pSwapChain, VkPhysicalDeviceMemoryProperties& memoryProperties, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBufferObject_s& bufferObject, void *data = nullptr);
	static VkResult SubmitBufferObjects(const VkCommandBuffer& copyCmd, const VkQueue& copyQueue, const VulkanRendererInitializer& pRendInit, VkDeviceSize size, VkBufferObject_s& stagingBuffer, VkBufferObject_s& localBuffer, enum drawVertFlags enumDrawDescriptors);

private:
	//Vertex
	static void BindVertexDescriptor(VkBufferObject_s& localBuffer);

	//Normal
	static void BindNormalDescriptor(VkBufferObject_s& localBuffer);

	//Uv
	static void BindUvDescriptor(VkBufferObject_s& localBuffer);

	//Vertex, Uv
	static void BindVertexUvDescriptor(VkBufferObject_s& localBuffer);

	//Vertex, Normal
	static void BindVertexNormalDescriptor(VkBufferObject_s& localBuffer);

	//Vertex, Normal, Uv
	static void BindVertexNormalUvDescriptor(VkBufferObject_s& localBuffer);

	//Vertex, Norma, Uv, Tangent, Binormal
	static void BindVertexNormalUvTangentBinormalDescriptor(VkBufferObject_s& localBuffer);
};