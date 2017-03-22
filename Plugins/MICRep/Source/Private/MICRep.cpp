// Fill out your copyright notice in the Description page of Project Settings.

#include "MICRep.h"
#include "LevelEditor.h"
#include "AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "AssetToolsModule.h"
#include "SAssetSearchBox.h"


#define LOCTEXT_NAMESPACE "MICRep"


class FMICRepModule : public IModuleInterface
{
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
	static void CreateAssetMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets);
	static void CreateReparentSubMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets);
	static void CreateReparentSubSubMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets);

	static void ReplaceMaterials(TArray<FAssetData> SelectedAssets);
	static UMaterialInterface* CreateMIC(UMaterialInterface* BaseMaterial, FString BaseMaterialSimpleName, UMaterialInterface* OldMaterial, FString TargetPathName);
	static void ReplaceStaticMeshMaterial(FAssetData& Asset);
	static void ReplaceSkeletalMeshMaterial(FAssetData& Asset);
	static void ReparentMICs(const FAssetData& NewParentAssetData, TArray<FAssetData> SelectedAssets);
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

	// アセット右クリックメニューへのExtender登録 
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
	FContentBrowserModule* ContentBrowserModule =
		FModuleManager::GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser"));
	if(nullptr != ContentBrowserModule)
	{
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenderDelegates =
			ContentBrowserModule->GetAllAssetViewContextMenuExtenders();
		CBMenuExtenderDelegates.RemoveAll([](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
			{ return Delegate.GetHandle() == ContentBrowserExtenderDelegateHandle; });
	}
}


TSharedRef<FExtender> FMICRepModule::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());

	bool bAnyMeshes = false;
	bool bAnyMICs = false;
	for(auto ItAsset = SelectedAssets.CreateConstIterator(); ItAsset; ++ItAsset)
	{
		bAnyMeshes |= ((*ItAsset).AssetClass == UStaticMesh::StaticClass()->GetFName());
		bAnyMeshes |= ((*ItAsset).AssetClass == USkeletalMesh::StaticClass()->GetFName());
		bAnyMICs   |= ((*ItAsset).AssetClass == UMaterialInstanceConstant::StaticClass()->GetFName());
	}

	if(bAnyMeshes | bAnyMICs)
	{
		Extender->AddMenuExtension(
			"GetAssetActions",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateStatic(&FMICRepModule::CreateAssetMenu, SelectedAssets)
			);
	}

	return Extender;
}
void FMICRepModule::CreateAssetMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	bool bAnyMeshes = false;
	bool bAnyMICs = false;
	for(auto ItAsset = SelectedAssets.CreateConstIterator(); ItAsset; ++ItAsset)
	{
		bAnyMeshes |= ((*ItAsset).AssetClass == UStaticMesh::StaticClass()->GetFName());
		bAnyMeshes |= ((*ItAsset).AssetClass == USkeletalMesh::StaticClass()->GetFName());
		bAnyMICs   |= ((*ItAsset).AssetClass == UMaterialInstanceConstant::StaticClass()->GetFName());
	}

	if(bAnyMeshes)
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
	if(bAnyMICs)
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("ReparentMaterialInstance", "Reparent MaterialInstance"),
			LOCTEXT("ReparentMaterialInstance_Tooltip", "Reparent MaterialInstance"),
			FNewMenuDelegate::CreateStatic(&FMICRepModule::CreateReparentSubMenu, SelectedAssets)
			);
	}
}
void FMICRepModule::CreateReparentSubMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	MenuBuilder.AddSubMenu(
		LOCTEXT("ReparentMaterialInstance", "Reparent MaterialInstance"),
		LOCTEXT("ReparentMaterialInstance_Tooltip", "Reparent MaterialInstance"),
		FNewMenuDelegate::CreateStatic(&FMICRepModule::CreateReparentSubSubMenu, SelectedAssets)
		);
}
void FMICRepModule::CreateReparentSubSubMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FAssetPickerConfig Config;
	Config.SelectionMode = ESelectionMode::Single;
	Config.InitialAssetViewType = EAssetViewType::List;
	Config.Filter.ClassNames.Add(FName(*UMaterialInterface::StaticClass()->GetName()));
	Config.Filter.bRecursiveClasses = true;
	Config.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateStatic(&FMICRepModule::ReparentMICs, SelectedAssets);
	Config.bFocusSearchBoxWhenOpened = true;

	MenuBuilder.AddWidget(ContentBrowserModule.Get().CreateAssetPicker(Config), FText());
}

//
// StaticMesh/SkeletalMeshマテリアルの一括置換 
//
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
		UObject* TargetAsset = MeshAssetData.GetAsset();
		if(nullptr == TargetAsset)
		{
			continue;
		}

		// ベースマテリアルを複製 
		FString TargetPathName = FPackageName::GetLongPackagePath(TargetAsset->GetPathName());
		UMaterial* BaseMat = nullptr;
		FString BaseMatSimpleName;
		{
			BaseMatSimpleName = TargetAsset->GetName().Replace(TEXT("SM_"), TEXT(""), ESearchCase::CaseSensitive);
			FString BaseMatName = FString::Printf(TEXT("M_%s_Base"), *BaseMatSimpleName);

			UObject* DuplicatedObject = AssetToolsModule.Get().DuplicateAsset(
				BaseMatName,
				TargetPathName,
				BaseMatOriginal
				);
			BaseMat = Cast<UMaterial>(DuplicatedObject);
			if (nullptr == BaseMat)
			{
				continue;
			}
		}

		// StaticMesh 
		UStaticMesh* TargetStaticMesh = Cast<UStaticMesh>(MeshAssetData.GetAsset());
		if(nullptr != TargetStaticMesh)
		{
			// メッシュの各マテリアルについて 
			int32 MatIdx = 0;
			for(auto ItMat = TargetStaticMesh->Materials.CreateConstIterator(); ItMat; ++ItMat, ++MatIdx)
			{
				UMaterialInterface* NewMIC = CreateMIC(
					BaseMat,
					BaseMatSimpleName,
					TargetStaticMesh->Materials[MatIdx],
					TargetPathName
					);
				if(nullptr == NewMIC)
				{
					continue;
				}
				ObjectsToSync.Add(NewMIC);

				// メッシュに新MICをセット 
				TargetStaticMesh->Materials[MatIdx] = NewMIC;
			}

			// メッシュアセットに要保存マーク 
			TargetStaticMesh->MarkPackageDirty();
		}

		// SkeletalMesh 
		USkeletalMesh* TargetSkeletalMesh = Cast<USkeletalMesh>(MeshAssetData.GetAsset());
		if(nullptr != TargetSkeletalMesh)
		{
			// メッシュの各マテリアルについて 
			int32 MatIdx = 0;
			for(auto ItMat = TargetSkeletalMesh->Materials.CreateConstIterator(); ItMat; ++ItMat, ++MatIdx)
			{
				UMaterialInterface* NewMIC = CreateMIC(
					BaseMat,
					BaseMatSimpleName,
					TargetSkeletalMesh->Materials[MatIdx].MaterialInterface,
					TargetPathName
					);
				if (nullptr == NewMIC)
				{
					continue;
				}
				ObjectsToSync.Add(NewMIC);

				// メッシュに新MICをセット 
				TargetSkeletalMesh->Materials[MatIdx].MaterialInterface = NewMIC;
			}

			// メッシュアセットに要保存マーク 
			TargetSkeletalMesh->MarkPackageDirty();
		}
	}

	if(0 < ObjectsToSync.Num())
	{
		ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync, true);
	}
}

UMaterialInterface* FMICRepModule::CreateMIC(
	UMaterialInterface* BaseMaterial,
	FString BaseMaterialSimpleName,
	UMaterialInterface* OldMaterial,
	FString TargetPathName
	)
{
	if((nullptr == BaseMaterial) || (nullptr == OldMaterial))
	{
		return nullptr;
	}

	FAssetToolsModule& AssetToolsModule =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	// 元マテリアル情報 
	UTexture* ColorTex = nullptr;
	{
		TArray<UTexture*> Textures;
		TArray<FName> TextureNames;
		OldMaterial->GetTexturesInPropertyChain(
			EMaterialProperty::MP_BaseColor,
			Textures,
			&TextureNames,
			nullptr
			);
		if (0 < Textures.Num())
		{
			ColorTex = Textures[0];
		}
	}
	UTexture* NormalTex = nullptr;
	{
		TArray<UTexture*> Textures;
		TArray<FName> TextureNames;
		OldMaterial->GetTexturesInPropertyChain(
			EMaterialProperty::MP_Normal,
			Textures,
			&TextureNames,
			nullptr
			);
		if (0 < Textures.Num())
		{
			NormalTex = Textures[0];
		}
	}

	// 新MIC名 
	FString NewMICName = FString::Printf(
		TEXT("MI_%s_%s"),
		*BaseMaterialSimpleName,
		*(OldMaterial->GetName().Replace(TEXT("M_"), TEXT(""), ESearchCase::CaseSensitive))
		);

	// 新MIC作成 
	UMaterialInstanceConstant* NewMIC = nullptr;
	{
		UMaterialInstanceConstantFactoryNew* Factory =
			NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = BaseMaterial;

		UObject* NewAsset = AssetToolsModule.Get().CreateAsset(
			NewMICName,
			TargetPathName,
			UMaterialInstanceConstant::StaticClass(),
			Factory
			);

		NewMIC = Cast<UMaterialInstanceConstant>(NewAsset);
	}
	if(nullptr == NewMIC)
	{
		return nullptr;
	}

	// 新MICへテクスチャ設定 
	FStaticParameterSet StaticParams;
	if (nullptr != ColorTex)
	{
		NewMIC->SetTextureParameterValueEditorOnly(
			FName(TEXT("BaseColor")),
			ColorTex
			);
	}
	if (nullptr != NormalTex)
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
	if (0 < StaticParams.StaticSwitchParameters.Num())
	{
		NewMIC->UpdateStaticPermutation(StaticParams);
	}

	return NewMIC;
}

//
// マテリアルの一括Reparent. 
//
void FMICRepModule::ReparentMICs(const FAssetData& NewParentAssetData, TArray<FAssetData> SelectedAssets)
{
	// 新たに親にするマテリアルを取得 
	UMaterialInterface* NewParent = Cast<UMaterialInterface>(NewParentAssetData.GetAsset());
	if(nullptr == NewParent)
	{
		return;
	}

	// 各選択アセットについて 
	TArray<UObject*> ObjectsToSync;
	for(auto ItAsset = SelectedAssets.CreateConstIterator(); ItAsset; ++ItAsset)
	{
		// 編集対象MICを取得 
		const FAssetData& MICAssetData = (*ItAsset);
		UMaterialInstanceConstant* TargetMIC = Cast<UMaterialInstanceConstant>(MICAssetData.GetAsset());
		if(nullptr == TargetMIC)
		{
			continue;
		}

		// 親マテリアルを変更 
		TargetMIC->SetParentEditorOnly(NewParent);
		TargetMIC->MarkPackageDirty();
		TargetMIC->PostEditChange();

		ObjectsToSync.Add(TargetMIC);
	}

	if(0 < ObjectsToSync.Num())
	{
		FContentBrowserModule& ContentBrowserModule = 
			FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

		ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync, true);
	}
}

#undef LOCTEXT_NAMESPACE
