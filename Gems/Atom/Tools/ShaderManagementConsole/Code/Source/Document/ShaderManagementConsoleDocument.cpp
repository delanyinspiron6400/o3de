/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AssetDatabase/AssetDatabaseConnection.h>
#include <Atom/RPI.Edit/Common/AssetUtils.h>
#include <Atom/RPI.Edit/Common/JsonUtils.h>
#include <Atom/RPI.Edit/Material/MaterialTypeSourceData.h>
#include <Atom/RPI.Edit/Shader/ShaderOptionValuesSourceData.h>
#include <Atom/RPI.Public/Material/Material.h>
#include <Atom/RPI.Reflect/Asset/AssetUtils.h>
#include <Atom/RPI.Reflect/Material/MaterialAsset.h>
#include <AtomToolsFramework/Document/AtomToolsDocumentNotificationBus.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Utils/Utils.h>
#include <AzFramework/StringFunc/StringFunc.h>
#include <AzToolsFramework/API/EditorAssetSystemAPI.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <Document/ShaderManagementConsoleDocument.h>
#include <HashedVariantListSourceData.h>

namespace ShaderManagementConsole
{
    void ShaderManagementConsoleDocument::Reflect(AZ::ReflectContext* context)
    {
        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<ShaderManagementConsoleDocument, AtomToolsFramework::AtomToolsDocument>()
                ->Version(0);
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ShaderManagementConsoleDocumentRequestBus>("ShaderManagementConsoleDocumentRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Editor")
                ->Attribute(AZ::Script::Attributes::Module, "shadermanagementconsole")
                ->Event("SetShaderVariantListSourceData", &ShaderManagementConsoleDocumentRequestBus::Events::SetShaderVariantListSourceData)
                ->Event("GetShaderVariantListSourceData", &ShaderManagementConsoleDocumentRequestBus::Events::GetShaderVariantListSourceData)
                ->Event("GetShaderOptionDescriptorCount", &ShaderManagementConsoleDocumentRequestBus::Events::GetShaderOptionDescriptorCount)
                ->Event("GetShaderOptionDescriptor", &ShaderManagementConsoleDocumentRequestBus::Events::GetShaderOptionDescriptor)
                ->Event("AppendSparseVariantSet", &ShaderManagementConsoleDocumentRequestBus::Events::AppendSparseVariantSet)
                ->Event("DefragmentVariantList", &ShaderManagementConsoleDocumentRequestBus::Events::DefragmentVariantList)
                ->Event("AddOneVariantRow", &ShaderManagementConsoleDocumentRequestBus::Events::AddOneVariantRow)
                ;
        }
    }

    ShaderManagementConsoleDocument::ShaderManagementConsoleDocument(
        const AZ::Crc32& toolId, const AtomToolsFramework::DocumentTypeInfo& documentTypeInfo)
        : AtomToolsFramework::AtomToolsDocument(toolId, documentTypeInfo)
    {
        ShaderManagementConsoleDocumentRequestBus::Handler::BusConnect(m_id);
    }

    ShaderManagementConsoleDocument::~ShaderManagementConsoleDocument()
    {
        ShaderManagementConsoleDocumentRequestBus::Handler::BusDisconnect();
    }

    AZ::u32 ShaderManagementConsoleDocument::AddOneVariantRow()
    {
        auto shaderAssetResult =
            AZ::RPI::AssetUtils::LoadAsset<AZ::RPI::ShaderAsset>(m_absolutePath,
                                                                 m_shaderVariantListSourceData.m_shaderFilePath);
        if (shaderAssetResult)

        {
            AZ::Data::Asset<AZ::RPI::ShaderAsset> shaderAsset = shaderAssetResult.GetValue();
            AZStd::vector<AZ::RPI::ShaderOptionDescriptor> options = shaderAsset->GetShaderOptionGroupLayout()->GetShaderOptions();
            AZ::RPI::ShaderVariantListSourceData::VariantInfo variantInfo;
            variantInfo.m_stableId = m_shaderVariantListSourceData.m_shaderVariants.empty()
                ? 1  // stable ID start at 1, since 0 is reserved as explained in ShaderVariantTreeAssetCreator
                : m_shaderVariantListSourceData.m_shaderVariants.back().m_stableId + 1;
            m_shaderVariantListSourceData.m_shaderVariants.push_back(variantInfo);
            return variantInfo.m_stableId;
        }
        return {};
    }

    void ShaderManagementConsoleDocument::AppendSparseVariantSet(
        AZStd::vector<AZ::Name> optionHeaders,
        AZStd::vector<AZ::Name> matrixOfValues)
    {
        // Make a lookup table to "reverse" the vector given to us in argument
        AZStd::unordered_map<AZ::Name, int> nameToHeaderIndex;
        for (int i = 0; i < optionHeaders.size(); ++i)
        {
            nameToHeaderIndex[optionHeaders[i]] = i;
        }
        // Prepare a whole new source data
        AZ::RPI::ShaderVariantListSourceData newSourceData{ m_shaderVariantListSourceData };
        AZ::u32 stableId = newSourceData.m_shaderVariants.empty() ? 1 : newSourceData.m_shaderVariants.back().m_stableId + 1;
        if (matrixOfValues.size() % optionHeaders.size() != 0)
        {
            AZ_Error("ShaderManagementConsoleDocument", false,
                "AppendSpareseVariantSet: matrixOfValues size must be multiple of header count");
            return;
        }
        // add "line by line"
        int numLines = matrixOfValues.size() / optionHeaders.size();
        for (int line = 0; line < numLines; ++line)
        {
            AZ::RPI::ShaderOptionValuesSourceData mapOfOptionNameToValues;
            size_t count = GetShaderOptionDescriptorCount();
            // we need to fill-in the gaps by adding empty values for unset options, so we loop on all descriptors
            for (int column = 0; column < count; ++column)
            {
                auto& descriptor = GetShaderOptionDescriptor(column);
                auto& optionName = descriptor.GetName();
                auto indexIt = nameToHeaderIndex.find(optionName);
                if (indexIt != nameToHeaderIndex.end())
                {
                    int index = line * optionHeaders.size() + indexIt->second;
                    mapOfOptionNameToValues[optionName] = matrixOfValues[index];
                }
            }
            AZ::RPI::ShaderVariantListSourceData::VariantInfo newLine{ stableId++, mapOfOptionNameToValues };
            newSourceData.m_shaderVariants.emplace_back(std::move(newLine));
        }
        SetShaderVariantListSourceData(newSourceData);
    }

    void ShaderManagementConsoleDocument::DefragmentVariantList()
    {
        struct VariantCompacterKey
        {
            AZ::RPI::ShaderVariantListSourceData::VariantInfo* m_info{};
            size_t m_hash{};

            bool operator==(const VariantCompacterKey& rhs) const
            {
                return m_hash == rhs.m_hash && m_info->m_options == rhs.m_info->m_options;  // first part of expression for short circuit
            }

            static VariantCompacterKey Make(AZ::RPI::ShaderVariantListSourceData::VariantInfo* source)
            {
                VariantCompacterKey newKey;
                newKey.m_info = source;
                newKey.m_hash = AZ::ShaderBuilder::HashedVariantInfoSourceData::HashCombineShaderOptionValues(0, source->m_options);
                return newKey;
            }
        };

        struct KeyHasher
        {
            std::size_t operator()(const VariantCompacterKey& key) const
            {
                return key.m_hash;
            }
        };
        // Use a set for uniquification process
        AZStd::unordered_set<VariantCompacterKey, KeyHasher> compacter;
        compacter.reserve(m_shaderVariantListSourceData.m_shaderVariants.size());
        for (auto& variantInfo : m_shaderVariantListSourceData.m_shaderVariants)
        {
            compacter.insert(VariantCompacterKey::Make(&variantInfo));
        }
        // Prepare a whole new source data
        AZ::RPI::ShaderVariantListSourceData newSourceData;
        // partial copy
        newSourceData.m_materialOptionsHint = m_shaderVariantListSourceData.m_materialOptionsHint;
        newSourceData.m_shaderFilePath = m_shaderVariantListSourceData.m_shaderFilePath;
        // variants are prepared from the compacted set
        newSourceData.m_shaderVariants.reserve(compacter.size());
        for (VariantCompacterKey& compactedKey : compacter)
        {
            newSourceData.m_shaderVariants.emplace_back(std::move(*compactedKey.m_info));
        }
        // sort by old stable id
        AZStd::sort(newSourceData.m_shaderVariants.begin(),
                    newSourceData.m_shaderVariants.end(),
                    [&](const AZ::RPI::ShaderVariantListSourceData::VariantInfo& a,
                        const AZ::RPI::ShaderVariantListSourceData::VariantInfo& b)
                    {
                        return a.m_stableId < b.m_stableId;
                    });
        // reassign stable ids completely, but based on old order
        AZ::u32 idCounter = 1;  // start at 1 (0 is reserved as explained in ShaderVariantTreeAssetCreator)
        for (auto& variantInfo : newSourceData.m_shaderVariants)
        {
            variantInfo.m_stableId = idCounter++;
        }
        SetShaderVariantListSourceData(newSourceData);
    }

    void ShaderManagementConsoleDocument::SetShaderVariantListSourceData(
        const AZ::RPI::ShaderVariantListSourceData& shaderVariantListSourceData)
    {
        m_shaderVariantListSourceData = shaderVariantListSourceData;
        AZStd::string shaderPath = m_shaderVariantListSourceData.m_shaderFilePath;

        auto shaderAssetResult = AZ::RPI::AssetUtils::LoadAsset<AZ::RPI::ShaderAsset>(m_absolutePath, shaderPath);
        if (shaderAssetResult)
        {
            m_shaderAsset = shaderAssetResult.GetValue();

            // We consider an empty shader variant list data set, a request for initialization
            if (m_shaderVariantListSourceData.m_shaderVariants.empty())
            {
                // Read system option file
                AZ::IO::Path fullPath = AZ::IO::Path(AZ::RPI::AssetUtils::ResolvePathReference(m_absolutePath, shaderPath));
                fullPath.ReplaceExtension("systemoptions");

                AZ::RPI::ShaderOptionValuesSourceData systemOptionSetting;
                if (!AZ::RPI::JsonUtils::LoadObjectFromFile(fullPath.String(), systemOptionSetting))
                {
                    AZ_Warning("ShaderManagementConsoleDocument", false, "System option setting not found : '%s.'", fullPath.c_str());
                }

                if (systemOptionSetting.size() > 0)
                {
                    AZ::u32 stableId = 1;
                    AZStd::vector<AZ::RPI::ShaderOptionDescriptor> unsetOption;
                    const auto& shaderOptionDescriptors = m_shaderAsset->GetShaderOptionGroupLayout()->GetShaderOptions();

                    // Check user input with descriptor from shader asset
                    for (auto& shaderOptionDescriptor : shaderOptionDescriptors)
                    {
                        AZ::Name optionName = shaderOptionDescriptor.GetName();
                        const auto optionIt = systemOptionSetting.find(optionName);
                        if (optionIt != systemOptionSetting.end())
                        {
                            AZ::Name valueName = AZ::Name(optionIt->second);
                            if (strcmp(valueName.GetCStr(), "") == 0)
                            {
                                // Option with unset value, expand later
                                unsetOption.push_back(shaderOptionDescriptor);
                                systemOptionSetting[optionName] = shaderOptionDescriptor.GetDefaultValue();
                            }
                        }
                    }

                    // Get total number of variants
                    size_t totalVariantSize = 1;
                    for (auto& shaderOptionDescriptor : unsetOption)
                    {
                        uint32_t minValue = shaderOptionDescriptor.GetMinValue().GetIndex();
                        uint32_t maxValue = shaderOptionDescriptor.GetMaxValue().GetIndex();
                        totalVariantSize = totalVariantSize * (maxValue - minValue + 1);
                    }
                    m_shaderVariantListSourceData.m_shaderVariants.reserve(totalVariantSize);
                    m_shaderVariantListSourceData.m_shaderVariants.emplace_back(stableId, systemOptionSetting);
                    stableId++;

                    // Expand unset option
                    for (auto& shaderOptionDescriptor : unsetOption)
                    {
                        AZStd::vector<AZ::RPI::ShaderVariantListSourceData::VariantInfo> shaderVariants;
                        AZStd::vector<AZ::RPI::ShaderVariantListSourceData::VariantInfo> expandShaderVariants;

                        uint32_t minValue = shaderOptionDescriptor.GetMinValue().GetIndex();
                        uint32_t maxValue = shaderOptionDescriptor.GetMaxValue().GetIndex();
                        size_t listSize = m_shaderVariantListSourceData.m_shaderVariants.size();
                        size_t expandSize = listSize * (maxValue - minValue);
                        shaderVariants.reserve(listSize);
                        expandShaderVariants.reserve(expandSize);

                        for (uint32_t index = minValue; index <= maxValue; ++index)
                        {
                            AZ::Name optionValue = shaderOptionDescriptor.GetValueName(index);
                            if (optionValue != shaderOptionDescriptor.GetDefaultValue())
                            {
                                stableId = UpdateOptionValue(
                                    m_shaderVariantListSourceData.m_shaderVariants,
                                    shaderVariants,
                                    shaderOptionDescriptor.GetName(),
                                    optionValue,
                                    stableId);

                                expandShaderVariants.insert(
                                    expandShaderVariants.end(),
                                    AZStd::make_move_iterator(shaderVariants.begin()),
                                    AZStd::make_move_iterator(shaderVariants.end()));
                            }
                        }

                        m_shaderVariantListSourceData.m_shaderVariants.insert(
                            m_shaderVariantListSourceData.m_shaderVariants.end(),
                            AZStd::make_move_iterator(expandShaderVariants.begin()),
                            AZStd::make_move_iterator(expandShaderVariants.end()));
                    }
                }
            }
            

            AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
                m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentObjectInfoInvalidated, m_id);
            AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
                m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentModified, m_id);
            m_modified = true;
        }
        else
        {
            AZ_Error("ShaderManagementConsoleDocument", false, "Could not load shader asset: %s.", shaderPath.c_str());
        }
    }

    const AZ::RPI::ShaderVariantListSourceData& ShaderManagementConsoleDocument::GetShaderVariantListSourceData() const
    {
        return m_shaderVariantListSourceData;
    }

    size_t ShaderManagementConsoleDocument::GetShaderOptionDescriptorCount() const
    {
        if (m_shaderAsset.IsReady())
        {
            const auto& layout = m_shaderAsset->GetShaderOptionGroupLayout();
            const auto& shaderOptionDescriptors = layout->GetShaderOptions();
            return shaderOptionDescriptors.size();
        }
        return 0;
    }

    const AZ::RPI::ShaderOptionDescriptor& ShaderManagementConsoleDocument::GetShaderOptionDescriptor(size_t index) const
    {
        if (m_shaderAsset.IsReady())
        {
            const auto& layout = m_shaderAsset->GetShaderOptionGroupLayout();
            const auto& shaderOptionDescriptors = layout->GetShaderOptions();
            return shaderOptionDescriptors.at(index);
        }
        AZ_Error("ShaderManagementConsoleDocument", false, "GetShaderOptionDescriptor no asset ready");
        return m_invalidDescriptor;
    }

    AtomToolsFramework::DocumentTypeInfo ShaderManagementConsoleDocument::BuildDocumentTypeInfo()
    {
        AtomToolsFramework::DocumentTypeInfo documentType;
        documentType.m_documentTypeName = "Shader Variant List";
        documentType.m_documentFactoryCallback = [](const AZ::Crc32& toolId, const AtomToolsFramework::DocumentTypeInfo& documentTypeInfo) {
            return aznew ShaderManagementConsoleDocument(toolId, documentTypeInfo); };
        documentType.m_supportedExtensionsToOpen.push_back({ "Shader Variant List", AZ::RPI::ShaderVariantListSourceData::Extension });
        documentType.m_supportedExtensionsToCreate.push_back({ "Shader Asset", AZ::RPI::ShaderSourceData::Extension });
        documentType.m_supportedExtensionsToSave.push_back({ "Shader Variant List", AZ::RPI::ShaderVariantListSourceData::Extension });
        return documentType;
    }

    AtomToolsFramework::DocumentObjectInfoVector ShaderManagementConsoleDocument::GetObjectInfo() const
    {
        AtomToolsFramework::DocumentObjectInfoVector objects = AtomToolsDocument::GetObjectInfo();

        AtomToolsFramework::DocumentObjectInfo objectInfo;
        objectInfo.m_visible = true;
        objectInfo.m_name = "Shader Variant List";
        objectInfo.m_displayName = "Shader Variant List";
        objectInfo.m_description = "Shader Variant List";
        objectInfo.m_objectType = azrtti_typeid<AZ::RPI::ShaderVariantListSourceData>();
        objectInfo.m_objectPtr = const_cast<AZ::RPI::ShaderVariantListSourceData*>(&m_shaderVariantListSourceData);
        objects.push_back(AZStd::move(objectInfo));

        return objects;
    }

    bool ShaderManagementConsoleDocument::Open(const AZStd::string& loadPath)
    {
        if (!AtomToolsDocument::Open(loadPath))
        {
            // SaveFailed has already been called so just forward the result without additional notifications.
            // TODO Replace bool return value with enum for open and save states.
            return false;
        }

        if (AzFramework::StringFunc::Path::IsExtension(m_absolutePath.c_str(), AZ::RPI::ShaderSourceData::Extension))
        {
            return LoadShaderSourceData();
        }

        if (AzFramework::StringFunc::Path::IsExtension(m_absolutePath.c_str(), AZ::RPI::ShaderVariantListSourceData::Extension))
        {
            return LoadShaderVariantListSourceData();
        }

        AZ_Error("ShaderManagementConsoleDocument", false, "Document extension is not supported: '%s.'", m_absolutePath.c_str());
        return OpenFailed();
    }

    bool ShaderManagementConsoleDocument::Save()
    {
        if (!AtomToolsDocument::Save())
        {
            // SaveFailed has already been called so just forward the result without additional notifications.
            // TODO Replace bool return value with enum for open and save states.
            return false;
        }

        return SaveSourceData();
    }

    bool ShaderManagementConsoleDocument::SaveAsCopy(const AZStd::string& savePath)
    {
        if (!AtomToolsDocument::SaveAsCopy(savePath))
        {
            // SaveFailed has already been called so just forward the result without additional notifications.
            // TODO Replace bool return value with enum for open and save states.
            return false;
        }

        return SaveSourceData();
    }

    bool ShaderManagementConsoleDocument::SaveAsChild(const AZStd::string& savePath)
    {
        if (!AtomToolsDocument::SaveAsChild(savePath))
        {
            // SaveFailed has already been called so just forward the result without additional notifications.
            // TODO Replace bool return value with enum for open and save states.
            return false;
        }

        return SaveSourceData();
    }

    bool ShaderManagementConsoleDocument::IsModified() const
    {
        return m_modified;
    }

    bool ShaderManagementConsoleDocument::BeginEdit()
    {
        // Save the current properties as a momento for undo before any changes are applied
        m_shaderVariantListSourceDataBeforeEdit = m_shaderVariantListSourceData;
        return true;
    }

    bool ShaderManagementConsoleDocument::EndEdit()
    {
        bool modified = false;

        // Lazy evaluation, comparing the current and previous shader variant list source data state to determine if we need to record undo/redo history.
        // TODO Refine this so that only the deltas are stored.
        const auto& undoState = m_shaderVariantListSourceDataBeforeEdit;
        const auto& redoState = m_shaderVariantListSourceData;
        if (undoState.m_shaderFilePath != redoState.m_shaderFilePath ||
            undoState.m_shaderVariants.size() != redoState.m_shaderVariants.size())
        {
            modified = true;
        }
        else
        {
            for (size_t i = 0; i < redoState.m_shaderVariants.size(); ++i)
            {
                if (undoState.m_shaderVariants[i].m_stableId != redoState.m_shaderVariants[i].m_stableId ||
                    undoState.m_shaderVariants[i].m_options != redoState.m_shaderVariants[i].m_options)
                {
                    modified = true;
                    break;
                }
            }
        }

        if (modified)
        {
            AddUndoRedoHistory(
                [this, undoState]() { SetShaderVariantListSourceData(undoState); },
                [this, redoState]() { SetShaderVariantListSourceData(redoState); });

            AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
                m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentObjectInfoInvalidated, m_id);
            AtomToolsFramework::AtomToolsDocumentNotificationBus::Event(
                m_toolId, &AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentModified, m_id);
        }

        m_shaderVariantListSourceDataBeforeEdit = {};
        return true;
    }

    void ShaderManagementConsoleDocument::Clear()
    {
        AtomToolsFramework::AtomToolsDocument::Clear();

        m_shaderVariantListSourceData = {};
        m_shaderVariantListSourceDataBeforeEdit = {};
        m_shaderAsset = {};
        m_modified = {};
    }

    bool ShaderManagementConsoleDocument::SaveSourceData()
    {
        if (!AZ::RPI::JsonUtils::SaveObjectToFile(m_savePathNormalized, m_shaderVariantListSourceData))
        {
            AZ_Error("ShaderManagementConsoleDocument", false, "Document could not be saved: '%s'.", m_savePathNormalized.c_str());
            return SaveFailed();
        }

        m_absolutePath = m_savePathNormalized;
        m_modified = {};
        return SaveSucceeded();
    }

    bool ShaderManagementConsoleDocument::LoadShaderSourceData()
    {
        AZ::RPI::ShaderVariantListSourceData shaderVariantListSourceData;
        shaderVariantListSourceData.m_shaderFilePath = m_absolutePath;
        SetShaderVariantListSourceData(shaderVariantListSourceData);
        m_modified = {};
        return OpenSucceeded();
    }

    bool ShaderManagementConsoleDocument::LoadShaderVariantListSourceData()
    {
        // Load previously generated shader variant list source data
        AZ::RPI::ShaderVariantListSourceData shaderVariantListSourceData;
        if (!AZ::RPI::JsonUtils::LoadObjectFromFile(m_absolutePath, shaderVariantListSourceData))
        {
            AZ_Error("ShaderManagementConsoleDocument", false, "Failed loading shader variant list data: '%s.'", m_absolutePath.c_str());
            return OpenFailed();
        }

        SetShaderVariantListSourceData(shaderVariantListSourceData);
        m_modified = {};

        return OpenSucceeded();
    }

    AZ::u32 ShaderManagementConsoleDocument::UpdateOptionValue(
        AZStd::vector<AZ::RPI::ShaderVariantListSourceData::VariantInfo>& shaderVariantIN,
        AZStd::vector<AZ::RPI::ShaderVariantListSourceData::VariantInfo>& shaderVariantOUT,
        AZ::Name targetOption,
        AZ::Name targetValue,
        AZ::u32 stableId)
    {
        shaderVariantOUT.clear();

        for (auto& variantInfo : shaderVariantIN)
        {
            AZ::RPI::ShaderOptionValuesSourceData optionList;
            optionList = variantInfo.m_options;
            if (optionList.count(targetOption) > 0)
            {
                optionList[targetOption] = targetValue;
            }
            shaderVariantOUT.emplace_back(stableId, optionList);
            stableId += 1;
        }
        return stableId;
    }
} // namespace ShaderManagementConsole
