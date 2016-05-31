// Fill out your copyright notice in the Description page of Project Settings.

#include "MICRep.h"
#include "LevelEditor.h"
#include "AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "AssetToolsModule.h"


#define LOCTEXT_NAMESPACE "MICRep"


class FMICRepModule : public IModuleInterface
{
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
	static void CreateAssetMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets);
	static void ReplaceMaterials(TArray<FAssetData> SelectedAssets);
};

IMPLEMENT_MODULE(FMICRepModule, MICRepModule)

namespace
{
	FContentBrowserMenuExtender_SelectedAssets ContentBrowserExtenderDelegate;
	FDelegateHandle ContentBrowserExtenderDelegateHandle;
}



void FMICRepModule::StartupModule()
{
	if(IsRunningCommandlet()){ return; }

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	ContentBrowserExtenderDelegate =
		FContentBrowserMenuExtender_SelectedAssets::CreateStatic(
			&FMICRepModule::OnExtendContentBrowserAssetSelectionMenu
			);
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates =
		ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBMenuExtenderDelegates.Add(ContentBrowserExtenderDelegate);
	ContentBrowserExtenderDelegateHandle = CBMenuExtenderDelegates.Last().GetHandle();
}
void FMICRepModule::ShutdownModule()
{
	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates =
		ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBMenuExtenderDelegates.RemoveAll([](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
		{ return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle; });
}


TSharedRef<FExtender> FMICRepModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());

	bool bAnyMeshes = false;
	for(auto ItAsset = SelectedAssets.CreateConstIterator(); ItAsset; ++ItAsset)
	{
		bAnyMeshes |= ((*ItAsset).AssetClass == UStaticMesh::StaticClass()->GetFName());
	}

	if (bAnyMeshes)
	{
		Extender->AddMenuExtension(
			"GetAssetActions",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateStatic(&FMICRepModule::CreateAssetMenu, SelectedAssets));
	}

	return Extender;
}
void FMICRepModule::CreateAssetMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ReplaceMaterials", "ReplaceMaterials"),
		LOCTEXT("ReplaceMaterials_Tooltip", "Replace all Materials to MaterialInstance"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FMICRepModule::ReplaceMaterials, SelectedAssets)),
		NAME_None,
		EUserInterfaceActionType::Button
		);
}

void FMICRepModule::ReplaceMaterials(TArray<FAssetData> SelectedAssets)
{
	FAssetRegistryModule&  AssetRegistryModule  = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FAssetToolsModule&     AssetToolsModule     = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	// ベースマテリアルの複製元を取得 
	UMaterial* BaseMatOriginal = nullptr;
	{
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(TEXT("/MICRep/M_MICRepBase.M_MICRepBase")));
		BaseMatOriginal = Cast<UMaterial>(AssetData.GetAsset());
		check(BaseMatOriginal);
	}

	TArray<UObject*> ObjectsToSync;
	for(auto ItAsset = SelectedAssets.CreateConstIterator(); ItAsset; ++ItAsset)
	{
		// 編集対象メッシュを取得 
		const FAssetData& MeshAssetData = (*ItAsset);
		UStaticMesh* TargetMesh = Cast<UStaticMesh>(MeshAssetData.GetAsset());
		if(nullptr == TargetMesh)
		{
			continue;
		}
		FString TargetPathName = FPackageName::GetLongPackagePath(TargetMesh->GetPathName());

		// ベースマテリアルを複製 
		UMaterial* BaseMat = nullptr;
		FString BaseMatSimpleName;
		{
			BaseMatSimpleName = TargetMesh->GetName().Replace(TEXT("SM_"), TEXT(""), ESearchCase::CaseSensitive);
			FString BaseMatName = FString::Printf(TEXT("M_%s_Base"), *BaseMatSimpleName);
			
			UObject* DuplicatedObject = AssetToolsModule.Get().DuplicateAsset(
				BaseMatName,
				TargetPathName,
				BaseMatOriginal
				);
			BaseMat = Cast<UMaterial>(DuplicatedObject);
			if(nullptr == BaseMat)
			{
				continue;
			}
		}

		// メッシュの各マテリアルについて 
		int32 MatIdx = 0;
		for(auto ItMat = TargetMesh->Materials.CreateConstIterator(); ItMat; ++ItMat, ++MatIdx)
		{
			// 元マテリアル情報 
			UMaterialInterface* Mat = TargetMesh->Materials[MatIdx];
			if(nullptr == Mat)
			{
				continue;
			}
			UTexture* BaseColorTex = nullptr;
			{
				TArray<UTexture*> Textures;
				TArray<FName> TextureNames;
				Mat->GetTexturesInPropertyChain(
					EMaterialProperty::MP_BaseColor,
					Textures,
					&TextureNames,
					nullptr
					);
				if(0 < Textures.Num())
				{
					BaseColorTex = Textures[0];
				}
			}
			UTexture* NormalTex = nullptr;
			{
				TArray<UTexture*> Textures;
				TArray<FName> TextureNames;
				Mat->GetTexturesInPropertyChain(
					EMaterialProperty::MP_Normal,
					Textures,
					&TextureNames,
					nullptr
					);
				if(0 < Textures.Num())
				{
					NormalTex = Textures[0];
				}
			}

			// 新MIC名 
			FString NewMICName = FString::Printf(
				TEXT("MI_%s_%s"),
				*BaseMatSimpleName,
				*(Mat->GetName().Replace(TEXT("M_"), TEXT(""), ESearchCase::CaseSensitive))
				);

			// 新MIC作成 
			UMaterialInstanceConstant* NewMIC = nullptr;
			{
				UMaterialInstanceConstantFactoryNew* Factory =
					NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = BaseMat;

				UObject* NewAsset = AssetToolsModule.Get().CreateAsset(
					NewMICName,
					TargetPathName,
					UMaterialInstanceConstant::StaticClass(),
					Factory
					);
				ObjectsToSync.Add(NewAsset);

				NewMIC = Cast<UMaterialInstanceConstant>(NewAsset);
			}
			if(nullptr == NewMIC)
			{
				continue;
			}

			// 新MICへテクスチャ設定 
			FStaticParameterSet StaticParams;
			if(nullptr != BaseColorTex)
			{
				NewMIC->SetTextureParameterValueEditorOnly(
					FName(TEXT("BaseColor")),
					BaseColorTex
					);
			}
			if(nullptr != NormalTex)
			{
				NewMIC->SetTextureParameterValueEditorOnly(
					FName(TEXT("Normal")),
					NormalTex
					);
			}
			else
			{
				// NoramlMap不要な場合はStaticSwitchでオフにする 
				FStaticSwitchParameter Param;
				Param.ParameterName = FName("UseNormal");
				Param.Value = false;
				Param.bOverride = true;
				StaticParams.StaticSwitchParameters.Add(Param);
			}
			// StaticSwitchの適用 
			if(0 < StaticParams.StaticSwitchParameters.Num())
			{
				NewMIC->UpdateStaticPermutation(StaticParams);
			}

			// メッシュに新MICをセット 
			TargetMesh->Materials[MatIdx] = NewMIC;
		}

		// メッシュアセットに要保存マーク 
		TargetMesh->MarkPackageDirty();
	}

	if(0 < ObjectsToSync.Num())
	{
		ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync, true);
	}
}

#undef LOCTEXT_NAMESPACE
