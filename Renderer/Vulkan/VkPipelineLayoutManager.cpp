#include "VkPipelineLayoutManager.h"
#include "VkBufferObjectManager.h"

#include "VulkanTools.h"
#include "VulkanRendererInitializer.h"

namespace Renderer
{
	namespace Resource
	{
		void PipelineLayoutManager::CreateResource(const DOD::Ref& ref)
		{
			VkPipelineLayout& pipeline_layout = PipelineLayoutManager::GetPipelineLayout(ref);
			VkDescriptorSetLayout& descriptor_set_layout = PipelineLayoutManager::GetDescriptorSetLayout(ref);
			VkDescriptorPool& descriptor_pool = PipelineLayoutManager::GetDescriptorPool(ref);

			std::vector<VkDescriptorSetLayoutBinding>& set_layout_bindings = PipelineLayoutManager::GetDescriptorSetLayoutBinding(ref);

			VkDescriptorSetLayoutCreateInfo descriptor_layout = VkTools::Initializer::DescriptorSetLayoutCreateInfo(set_layout_bindings.data(), set_layout_bindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(VulkanSwapChain::device, &descriptor_layout, nullptr, &descriptor_set_layout));

			VkPipelineLayoutCreateInfo pipeline_layout_create_info = VkTools::Initializer::PipelineLayoutCreateInfo(&descriptor_set_layout, 1);
			VK_CHECK_RESULT(vkCreatePipelineLayout(VulkanSwapChain::device, &pipeline_layout_create_info, nullptr, &pipeline_layout));


			std::vector<VkDescriptorPoolSize> pool_size;
			pool_size.reserve(set_layout_bindings.size());

			uint32_t max_pool_count = 0u;
			for (auto& layout_binding : set_layout_bindings)
			{
				pool_size.push_back(VkTools::Initializer::DescriptorPoolSize(layout_binding.descriptorType, layout_binding.descriptorCount));
				max_pool_count = std::max(max_pool_count, layout_binding.descriptorCount);
			}

			VkDescriptorPoolCreateInfo descriptorPoolInfo = VkTools::Initializer::DescriptorPoolCreateInfo(pool_size.size(), pool_size.data(), max_pool_count);
			VK_CHECK_RESULT(vkCreateDescriptorPool(VulkanSwapChain::device, &descriptorPoolInfo, nullptr, &descriptor_pool));
		}

		void PipelineLayoutManager::AllocateWriteDescriptorSet(const DOD::Ref& ref, const std::vector<BindingInfo>& binding_infos)
		{
			VkDescriptorPool& descriptor_pool = PipelineLayoutManager::GetDescriptorPool(ref);
			VkDescriptorSetLayout& descriptor_set_layout = PipelineLayoutManager::GetDescriptorSetLayout(ref);
			
			
			VkDescriptorSetAllocateInfo descriptor_allocate_info = VkTools::Initializer::DescriptorSetAllocateInfo(descriptor_pool, &descriptor_set_layout, 1);

			VkDescriptorSet descriptor_set;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(VulkanSwapChain::device, &descriptor_allocate_info, &descriptor_set));

			auto& pipeline_layouts = PipelineLayoutManager::GetDescriptorSetLayoutBinding(ref);
			

			std::vector<VkWriteDescriptorSet>   write_descriptor_set;
			std::vector<VkDescriptorImageInfo>  image_infos;
			std::vector<VkDescriptorBufferInfo> buffer_infos;

			write_descriptor_set.resize(pipeline_layouts.size());
			image_infos.resize(pipeline_layouts.size());
			buffer_infos.resize(pipeline_layouts.size());

			for (uint32_t i = 0; i < pipeline_layouts.size(); i++)
			{
				auto& pipeline_layout = pipeline_layouts[i];

				const BindingInfo&     info      = binding_infos[i];
				BufferObject& buffer_object      = BufferObjectManager::GetBufferObject(info.buffer_ref);
				const VkDeviceSize size_in_bytes = BufferObjectManager::GetBufferSize(info.buffer_ref);

				VkDescriptorBufferInfo& buffer_info = buffer_infos[i];

				buffer_info.buffer = buffer_object.buffer;
				buffer_info.offset = 0u;
				buffer_info.range  = size_in_bytes;

				write_descriptor_set[i] = VkTools::Initializer::WriteDescriptorSet(descriptor_set, pipeline_layout.descriptorType, pipeline_layout.binding, &buffer_info);
			}

			vkUpdateDescriptorSets(VulkanSwapChain::device, write_descriptor_set.size(), write_descriptor_set.data(), 0, NULL);
		}

		void PipelineLayoutManager::DestroyResources(const std::vector<DOD::Ref>& refs)
		{
			for (uint32_t i = 0; i < refs.size(); i++)
			{
				const DOD::Ref& ref = refs[i];

				VkPipelineLayout& pipeline_layout = GetPipelineLayout(ref);
				if (pipeline_layout != VK_NULL_HANDLE)
				{
					vkDestroyPipelineLayout(VulkanSwapChain::device, pipeline_layout, nullptr);
					pipeline_layout = VK_NULL_HANDLE;
				}

				VkDescriptorSetLayout& descriptor_layout = GetDescriptorSetLayout(ref);
				if (descriptor_layout != VK_NULL_HANDLE)
				{
					vkDestroyDescriptorSetLayout(VulkanSwapChain::device, descriptor_layout, nullptr);
					descriptor_layout = VK_NULL_HANDLE;
				}

				VkDescriptorPool& descriptor_pool = GetDescriptorPool(ref);
				if (descriptor_pool != VK_NULL_HANDLE)
				{
					vkDestroyDescriptorPool(VulkanSwapChain::device, descriptor_pool, nullptr);
					descriptor_pool = VK_NULL_HANDLE;
				}
			}
		}
	}
}