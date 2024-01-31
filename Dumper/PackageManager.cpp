#include "PackageManager.h"
#include "ObjectArray.h"

/* Required for marking cyclic-headers in the StructManager */
#include "StructManager.h"

inline void BooleanOrEqual(bool& b1, bool b2)
{
	b1 = b1 || b2;
}

PackageInfoHandle::PackageInfoHandle(std::nullptr_t Nullptr)
	: Info(nullptr)
{
}

PackageInfoHandle::PackageInfoHandle(const PackageInfo& InInfo)
	: Info(&InInfo)
{
}

const StringEntry& PackageInfoHandle::GetNameEntry() const
{
	return PackageManager::GetPackageName(*Info);
}

int32 PackageInfoHandle::GetIndex() const
{
	return Info->PackageIndex;
}

std::string PackageInfoHandle::GetName() const
{
	const StringEntry& Name = GetNameEntry();

	if (Info->CollisionCount <= 0) [[likely]]
		return Name.GetName();

	return Name.GetName() + "_" + std::to_string(Info->CollisionCount - 1);
}

std::pair<std::string, uint8> PackageInfoHandle::GetNameCollisionPair() const
{
	const StringEntry& Name = GetNameEntry();

	if (Name.IsUniqueInTable()) [[likely]]
		return { Name.GetName(), 0 };

	return { Name.GetName(), Info->CollisionCount };
}

bool PackageInfoHandle::HasClasses() const
{
	return Info->ClassesSorted.GetNumEntries() > 0x0;
}

bool PackageInfoHandle::HasStructs() const
{
	return Info->StructsSorted.GetNumEntries() > 0x0;
}

bool PackageInfoHandle::HasFunctions() const
{
	return !Info->Functions.empty();
}

bool PackageInfoHandle::HasParameterStructs() const
{
	return Info->bHasParams;
}

bool PackageInfoHandle::HasEnums() const
{
	return !Info->Enums.empty();
}

bool PackageInfoHandle::IsEmpty() const
{
	return !HasClasses() && !HasStructs() && !HasEnums() && !HasParameterStructs() && !HasFunctions();
}


const DependencyManager& PackageInfoHandle::GetSortedStructs() const
{
	return Info->StructsSorted;
}

const DependencyManager& PackageInfoHandle::GetSortedClasses() const
{
	return Info->ClassesSorted;
}

const std::vector<int32>& PackageInfoHandle::GetFunctions() const
{
	return Info->Functions;
}

const std::vector<int32>& PackageInfoHandle::GetEnums() const
{
	return Info->Enums;
}

const DependencyInfo& PackageInfoHandle::GetPackageDependencies() const
{
	return Info->PackageDependencies;
}

void PackageInfoHandle::ErasePackageDependencyFromStructs(int32 Package) const
{
	Info->PackageDependencies.StructsDependencies.erase(Package);
}

void PackageInfoHandle::ErasePackageDependencyFromClasses(int32 Package) const
{
	Info->PackageDependencies.ClassesDependencies.erase(Package);
}


namespace PackageManagerUtils
{
	void GetPropertyDependency(UEProperty Prop, std::unordered_set<int32>& Store)
	{
		if (Prop.IsA(EClassCastFlags::StructProperty))
		{
			Store.insert(Prop.Cast<UEStructProperty>().GetUnderlayingStruct().GetIndex());
		}
		else if (Prop.IsA(EClassCastFlags::EnumProperty))
		{
			if (UEObject Enum = Prop.Cast<UEEnumProperty>().GetEnum())
				Store.insert(Enum.GetIndex());
		}
		else if (Prop.IsA(EClassCastFlags::ByteProperty))
		{
			if (UEObject Enum = Prop.Cast<UEByteProperty>().GetEnum())
				Store.insert(Enum.GetIndex());
		}
		else if (Prop.IsA(EClassCastFlags::ArrayProperty))
		{
			GetPropertyDependency(Prop.Cast<UEArrayProperty>().GetInnerProperty(), Store);
		}
		else if (Prop.IsA(EClassCastFlags::SetProperty))
		{
			GetPropertyDependency(Prop.Cast<UESetProperty>().GetElementProperty(), Store);
		}
		else if (Prop.IsA(EClassCastFlags::MapProperty))
		{
			GetPropertyDependency(Prop.Cast<UEMapProperty>().GetKeyProperty(), Store);
			GetPropertyDependency(Prop.Cast<UEMapProperty>().GetValueProperty(), Store);
		}
	}

	std::unordered_set<int32> GetDependencies(UEStruct Struct, int32 StructIndex)
	{
		std::unordered_set<int32> Dependencies;

		const int32 StructIdx = Struct.GetIndex();

		for (UEProperty Property : Struct.GetProperties())
		{
			GetPropertyDependency(Property, Dependencies);
		}

		Dependencies.erase(StructIdx);

		return Dependencies;
	}

	inline void SetPackageDependencies(DependencyListType& DependencyTracker, const std::unordered_set<int32>& Dependencies, int32 StructPackageIdx, bool bAllowToIncludeOwnPackage = false)
	{
		for (int32 Dependency : Dependencies)
		{
			const int32 PackageIdx = ObjectArray::GetByIndex(Dependency).GetPackageIndex();


			if (bAllowToIncludeOwnPackage || PackageIdx != StructPackageIdx)
			{
				RequirementInfo& ReqInfo = DependencyTracker[PackageIdx];
				ReqInfo.PackageIdx = PackageIdx;
				ReqInfo.bShouldIncludeStructs = true; // Dependencies only contains structs/enums which are in the "PackageName_structs.hpp" file
			}
		}
	}

	inline void AddEnumPackageDependencies(DependencyListType& DependencyTracker, const std::unordered_set<int32>& Dependencies, int32 StructPackageIdx, bool bAllowToIncludeOwnPackage = false)
	{
		for (int32 Dependency : Dependencies)
		{
			UEObject DependencyObject = ObjectArray::GetByIndex(Dependency);

			if (!DependencyObject.IsA(EClassCastFlags::Enum))
				continue;

			const int32 PackageIdx = DependencyObject.GetPackageIndex();

			if (bAllowToIncludeOwnPackage || PackageIdx != StructPackageIdx)
			{
				RequirementInfo& ReqInfo = DependencyTracker[PackageIdx];
				ReqInfo.PackageIdx = PackageIdx;
				ReqInfo.bShouldIncludeStructs = true; // Dependencies only contains enums which are in the "PackageName_structs.hpp" file
			}
		}
	}

	inline void AddStructDependencies(DependencyManager& StructDependencies, const std::unordered_set<int32>& Dependenies, int32 StructIdx, int32 StructPackageIndex)
	{
		std::unordered_set<int32> TempSet;

		for (int32 DependencyStructIdx : Dependenies)
		{
			UEObject Obj = ObjectArray::GetByIndex(DependencyStructIdx);

			if (Obj.GetPackageIndex() == StructPackageIndex && !Obj.IsA(EClassCastFlags::Enum))
				TempSet.insert(DependencyStructIdx);
		}

		StructDependencies.SetDependencies(StructIdx, std::move(TempSet));
	}
}

void PackageManager::InitDependencies()
{
	// Collects all packages required to compile this file

	for (auto Obj : ObjectArray())
	{
		if (Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject))
			continue;

		int32 CurrentPackageIdx = Obj.GetPackageIndex();

		const bool bIsStruct = Obj.IsA(EClassCastFlags::Struct);
		const bool bIsClass = Obj.IsA(EClassCastFlags::Class);

		const bool bIsFunction = Obj.IsA(EClassCastFlags::Function);
		const bool bIsEnum = Obj.IsA(EClassCastFlags::Enum);

		if (bIsStruct && !bIsFunction)
		{
			PackageInfo& Info = PackageInfos[CurrentPackageIdx];
			Info.PackageIndex = CurrentPackageIdx;

			UEStruct ObjAsStruct = Obj.Cast<UEStruct>();

			const int32 StructIdx = ObjAsStruct.GetIndex();
			const int32 StructPackageIdx = ObjAsStruct.GetPackageIndex();

			DependencyListType& PackageDependencyList = bIsClass ? Info.PackageDependencies.ClassesDependencies : Info.PackageDependencies.StructsDependencies;
			DependencyManager& ClassOrStructDependencyList = bIsClass ? Info.ClassesSorted : Info.StructsSorted;

			std::unordered_set<int32> Dependencies = PackageManagerUtils::GetDependencies(ObjAsStruct, StructIdx);

			ClassOrStructDependencyList.SetExists(StructIdx);

			PackageManagerUtils::SetPackageDependencies(PackageDependencyList, Dependencies, StructPackageIdx, bIsClass);

			if (!bIsClass)
				PackageManagerUtils::AddStructDependencies(ClassOrStructDependencyList, Dependencies, StructIdx, StructPackageIdx);

			/* for both struct and class */
			if (UEStruct Super = ObjAsStruct.GetSuper())
			{
				const int32 SuperPackageIdx = Super.GetPackageIndex();

				if (SuperPackageIdx == StructPackageIdx)
				{
					/* In-file sorting is only required if the super-class is inside of the same package */
					ClassOrStructDependencyList.AddDependency(Obj.GetIndex(), Super.GetIndex());
				}
				else
				{
					/* A package can't depend on itself, super of a structs will always be in _"structs" file, same for classes and "_classes" files */
					RequirementInfo& ReqInfo = PackageDependencyList[SuperPackageIdx];
					BooleanOrEqual(ReqInfo.bShouldIncludeStructs, !bIsClass);
					BooleanOrEqual(ReqInfo.bShouldIncludeClasses, bIsClass);
				}
			}

			if (!bIsClass)
				continue;
			
			/* Add class-functions to package */
			for (UEFunction Func : ObjAsStruct.GetFunctions())
			{
				Info.Functions.push_back(Func.GetIndex());

				std::unordered_set<int32> ParamDependencies = PackageManagerUtils::GetDependencies(Func, Func.GetIndex());

				BooleanOrEqual(Info.bHasParams, Func.HasMembers());

				const int32 FuncPackageIndex = Func.GetPackageIndex();

				/* Add dependencies to ParamDependencies and add enums only to class dependencies (forwarddeclaration of enum classes defaults to int) */
				PackageManagerUtils::SetPackageDependencies(Info.PackageDependencies.ParametersDependencies, ParamDependencies, FuncPackageIndex, true);
				PackageManagerUtils::AddEnumPackageDependencies(Info.PackageDependencies.ClassesDependencies, ParamDependencies, FuncPackageIndex, true);
			}
		}
		else if (bIsEnum)
		{
			PackageInfo& Info = PackageInfos[CurrentPackageIdx];
			Info.PackageIndex = CurrentPackageIdx;

			Info.Enums.push_back(Obj.GetIndex());
		}
	}
}

void PackageManager::InitNames()
{
	for (auto& [PackageIdx, Info] : PackageInfos)
	{
		std::string PackageName = ObjectArray::GetByIndex(PackageIdx).GetValidName();

		auto [Name, bWasInserted] = UniquePackageNameTable.FindOrAdd(PackageName);
		Info.Name = Name;

		if (!bWasInserted) [[unlikely]]
			Info.CollisionCount = UniquePackageNameTable[Name].GetCollisionCount().CollisionCount;
	}
}

void PackageManager::HelperMarkStructDependenciesOfPackage(UEStruct Struct, int32 OwnPackageIdx, int32 RequiredPackageIdx, bool bIsClass)
{
	if (UEStruct Super = Struct.GetSuper())
	{
		if (Super.GetPackageIndex() == RequiredPackageIdx)
			StructManager::PackageManagerSetCycleForStruct(Struct.GetIndex(), OwnPackageIdx);
	}

	if (bIsClass)
		return;

	for (UEProperty Child : Struct.GetProperties())
	{
		if (!Child.IsA(EClassCastFlags::StructProperty))
			continue;

		const int32 UnderlayingStructPackageIdx = Child.Cast<UEStructProperty>().GetUnderlayingStruct().GetPackageIndex();

		if (UnderlayingStructPackageIdx == RequiredPackageIdx)
			StructManager::PackageManagerSetCycleForStruct(Struct.GetIndex(), OwnPackageIdx);
	}
}

int32 PackageManager::HelperCountStructDependenciesOfPackage(UEStruct Struct, int32 RequiredPackageIdx, bool bIsClass)
{
	int32 RetCount = 0x0;

	if (UEStruct Super = Struct.GetSuper())
	{
		if (Super.GetPackageIndex() == RequiredPackageIdx)
			RetCount++;
	}

	if (bIsClass)
		return RetCount;

	for (UEProperty Child : Struct.GetProperties())
	{
		if (!Child.IsA(EClassCastFlags::StructProperty))
			continue;

		const int32 UnderlayingStructPackageIdx = Child.Cast<UEStructProperty>().GetUnderlayingStruct().GetPackageIndex();

		if (UnderlayingStructPackageIdx == RequiredPackageIdx)
			RetCount++;
	}

	return RetCount;
}

/* Safe to use StructManager, initialization is guaranteed to have been finished */
void PackageManager::HandleCycles()
{
	struct CycleInfo
	{
		int32 CurrentPackage;
		int32 PreviousPacakge;

		bool bAreStructsCyclic;
		bool bAreclassesCyclic;
	};

	std::vector<CycleInfo> HandledPackages;

	FindCycleCallbackType OnCycleFoundCallback = [&HandledPackages](const PackageManagerIterationParams& OldParams, const PackageManagerIterationParams& NewParams, bool bIsStruct) -> void
	{
		const int32 CurrentPackageIndex = NewParams.RequiredPackge;
		const int32 PreviousPackageIndex = NewParams.PrevPackage;

		/* Check if this pacakge was handled before, return if true */
		for (const CycleInfo& Cycle : HandledPackages)
		{
			if (((Cycle.CurrentPackage == CurrentPackageIndex && Cycle.PreviousPacakge == PreviousPackageIndex)
				|| (Cycle.CurrentPackage == PreviousPackageIndex && Cycle.PreviousPacakge == CurrentPackageIndex))
				&& (Cycle.bAreStructsCyclic == bIsStruct || Cycle.bAreclassesCyclic == !bIsStruct))
			{
				return;
			}
		}

		/* Current cyclic packages will be added to 'HandledPackages' later on in this function */

		/*
		* Example-cycle: a -> b -> c -> a
		* 
		* 1. Get all structs | classes from this package (GetInfo().StructDependencies)
		* 2. Find all structs/classes from 'c' that are used in 'a'
		* 3. Mark them as StructInfo::bIsPartOfCyclicDependencies = true;
		* 4. Add them to StructInfo::CyclicStructsAndPackages[StructIdx] = 'c.PackageIndex'
		*/

		const PackageInfoHandle CurrentPackageInfo = GetInfo(CurrentPackageIndex);
		const PackageInfoHandle PreviousPackageInfo = GetInfo(PreviousPackageIndex);

		const DependencyManager& CurrentStructsOrClasses = bIsStruct ? CurrentPackageInfo.GetSortedStructs() : CurrentPackageInfo.GetSortedClasses();
		const DependencyManager& PreviousStructsOrClasses = bIsStruct ? PreviousPackageInfo.GetSortedStructs() : PreviousPackageInfo.GetSortedClasses();

		int32 OutCount = 0x0;
		int32 PackageIndexToCheckOccurences = PreviousPackageIndex;

		/*
		* WRONG ---> GET THE STRUCT AND GET THE DEPDENDENCIES FROM THAT STRUCT MANUALLY
		*
		* if (Struct.getsuper().getpackage() == previouspackageindex)
		* {
		*		count++;
		*		StructManager::PackageManagerSetCycleForStruct(Struct.getsuper().index, previouspackageindex)
		* }
		*
		* if (!Struct.isstructnotclass)
		*	return/continue;
		*
		* for (child : Struct)
		* {
		*	if (child == structproperty && child.struct.getpackage() == previouspackageindex)
		*	{
		*		count++;
		*		StructManager::PackageManagerSetCycleForStruct(child.struct.index, previouspackageindex)
		*	}
		* }
		*/

		DependencyManager::OnVisitCallbackType CountDependencies = [&OutCount, &PackageIndexToCheckOccurences, &bIsStruct](int32 Index) -> void
		{
			OutCount += HelperCountStructDependenciesOfPackage(ObjectArray::GetByIndex<UEStruct>(Index), PackageIndexToCheckOccurences, !bIsStruct);
		};

		CurrentStructsOrClasses.VisitAllNodesWithCallback(CountDependencies);
		std::cout << "Count: 0x" << OutCount << std::endl;
		/* Number of structs from PreviousPackage required by CurrentPackage */
		const int32 NumStructsRequiredByCurrent = OutCount;

		OutCount = 0x0;
		PackageIndexToCheckOccurences = CurrentPackageIndex;
		CurrentStructsOrClasses.VisitAllNodesWithCallback(CountDependencies);
		std::cout << "Count: 0x" << OutCount << std::endl;

		OutCount = 0x0;
		PackageIndexToCheckOccurences = CurrentPackageIndex;
		PreviousStructsOrClasses.VisitAllNodesWithCallback(CountDependencies);

		/* Number of structs from CurrentPackage required by CurrentPackage PreviousPackage */
		const int32 NumStructsRequiredByPrevious = OutCount;


		std::string CurrentName = ObjectArray::GetByIndex(CurrentPackageIndex).GetValidName();
		std::string PreviousName = ObjectArray::GetByIndex(PreviousPackageIndex).GetValidName();

		std::cout << std::format("'{}' requires '0x{:04X} {}' from '{}'\n", CurrentName, NumStructsRequiredByCurrent, (bIsStruct ? "structs" : "classes"), PreviousName);
		std::cout << std::format("'{}' requires '0x{:04X} {}' from '{}'\n", PreviousName, NumStructsRequiredByPrevious, (bIsStruct ? "structs" : "classes"), CurrentName);


		/* Which of the two cyclic packages requires less structs from the other package. */
		const bool bCurrentHasMoreDependencies = NumStructsRequiredByCurrent > NumStructsRequiredByPrevious;

		/* Add both Current -> Previous and Previous -> Current */

		const int32 PackageIndexWithLeastDependencies = bCurrentHasMoreDependencies ? PreviousPackageIndex : CurrentPackageIndex;
		const int32 PackageIndexToMarkCyclicWith = bCurrentHasMoreDependencies ? CurrentPackageIndex : PreviousPackageIndex;

		HandledPackages.push_back({ PackageIndexWithLeastDependencies, PackageIndexToMarkCyclicWith, bIsStruct, !bIsStruct });

		DependencyManager::OnVisitCallbackType SetCycleCallback = [PackageIndexWithLeastDependencies, PackageIndexToMarkCyclicWith, bIsStruct](int32 Index) -> void
		{
			HelperMarkStructDependenciesOfPackage(ObjectArray::GetByIndex<UEStruct>(Index), PackageIndexToMarkCyclicWith, PackageIndexWithLeastDependencies, !bIsStruct);
		};

		PreviousStructsOrClasses.VisitAllNodesWithCallback(SetCycleCallback);
	};

	FindCycleCallbackType OnCycleFoundPrintCallback = [](const PackageManagerIterationParams& OldParams, const PackageManagerIterationParams& NewParams, bool bIsStruct) -> void
	{
		std::string PrevName = ObjectArray::GetByIndex(NewParams.PrevPackage).GetValidName() + (NewParams.bWasPrevNodeStructs ? "_structs" : "_classes");
		std::string CurrName = ObjectArray::GetByIndex(NewParams.RequiredPackge).GetValidName() + (bIsStruct ? "_structs" : "_classes");

		std::cout << std::format("Cycle between: Current - '{}' and Previous - '{}'\n", CurrName, PrevName);
	};

	FindCycle(OnCycleFoundCallback);

	for (const CycleInfo& Cycle : HandledPackages)
	{
		const PackageInfoHandle CurrentPackageInfo = GetInfo(Cycle.CurrentPackage);
		const PackageInfoHandle PreviousPackageInfo = GetInfo(Cycle.PreviousPacakge);

		if (Cycle.bAreStructsCyclic) {
			CurrentPackageInfo.ErasePackageDependencyFromStructs(Cycle.PreviousPacakge);
			//PreviousPackageInfo.ErasePackageDependencyFromStructs(Cycle.CurrentPackage);
		}
		else {
			/* check if some_classe.hpp could need somecyclic_structs.hpp, which is legal */
			const RequirementInfo& CurrentRequirements = CurrentPackageInfo.GetPackageDependencies().ClassesDependencies.at(Cycle.PreviousPacakge);
			const RequirementInfo& PreviousRequirements = PreviousPackageInfo.GetPackageDependencies().ClassesDependencies.at(Cycle.CurrentPackage);

			/* Mark classes as 'do not include' when this package is cyclic but can still require _structs.hpp */
			if (CurrentRequirements.bShouldIncludeStructs) {
				const_cast<RequirementInfo&>(CurrentRequirements).bShouldIncludeClasses = false;
			}
			else {
				CurrentPackageInfo.ErasePackageDependencyFromClasses(Cycle.PreviousPacakge);
			}

			//if (PreviousRequirements.bShouldIncludeStructs) {
			//	const_cast<RequirementInfo&>(PreviousRequirements).bShouldIncludeClasses = false;
			//}
			//else {
			//	CurrentPackageInfo.ErasePackageDependencyFromClasses(Cycle.CurrentPackage);
			//}
		}
	}
	/*
		if (bIsStruct) {
			CurrentPackageInfo.ErasePackageDependencyFromStructs(PreviousPackageIndex);
			PreviousPackageInfo.ErasePackageDependencyFromStructs(CurrentPackageIndex);
		}
		else {
			CurrentPackageInfo.ErasePackageDependencyFromClasses(PreviousPackageIndex);
			PreviousPackageInfo.ErasePackageDependencyFromClasses(CurrentPackageIndex);
		}
	*/
	//FindCycle(OnCycleFoundPrintCallback);
}

void PackageManager::Init()
{
	if (bIsInitialized)
		return;

	bIsInitialized = true;

	PackageInfos.reserve(0x800);

	InitDependencies();
	InitNames();
}

void PackageManager::PostInit()
{
	if (bIsPostInitialized)
		return;

	bIsPostInitialized = true;

	StructManager::Init();

	HandleCycles();
}

void PackageManager::IterateSingleDependencyImplementation(SingleDependencyIterationParamsInternal& Params, bool bCheckForCycle)
{
	if (!Params.bShouldHandlePackage)
		return;

	const bool bIsIncluded = Params.IterationHitCounterRef >= CurrentIterationHitCount;

	if (!bIsIncluded)
	{
		Params.IterationHitCounterRef = CurrentIterationHitCount;

		IncludeData& Include = Params.VisitedNodes[Params.CurrentIndex];
		Include.bIncludedStructs = (Include.bIncludedStructs || Params.bIsStruct);
		Include.bIncludedClasses = (Include.bIncludedClasses || !Params.bIsStruct);

		for (auto& [Index, Requirements] : Params.Dependencies)
		{
			Params.NewParams.bWasPrevNodeStructs = Params.bIsStruct;
			Params.NewParams.bRequiresClasses = Requirements.bShouldIncludeClasses;
			Params.NewParams.bRequiresStructs = Requirements.bShouldIncludeStructs;
			Params.NewParams.RequiredPackge = Requirements.PackageIdx;

			/* Iterate dependencies recursively */
			IterateDependenciesImplementation(Params.NewParams, Params.CallbackForEachPackage, Params.OnFoundCycle, bCheckForCycle);
		}

		Params.VisitedNodes.erase(Params.CurrentIndex);

		// PERFORM ACTION
		Params.CallbackForEachPackage(Params.NewParams, Params.OldParams, Params.bIsStruct);
		return;
	}

	if (bCheckForCycle)
	{
		auto It = Params.VisitedNodes.find(Params.CurrentIndex);
		if (It != Params.VisitedNodes.end())
		{
			if ((It->second.bIncludedStructs && Params.bIsStruct) || (It->second.bIncludedClasses && !Params.bIsStruct))
				Params.OnFoundCycle(Params.NewParams, Params.OldParams, Params.bIsStruct);
		}
	}
}

void PackageManager::IterateDependenciesImplementation(const PackageManagerIterationParams& Params, const IteratePackagesCallbackType& CallbackForEachPackage, const FindCycleCallbackType& OnFoundCycle, bool bCheckForCycle)
{
	PackageManagerIterationParams NewParams = {
		.PrevPackage = Params.RequiredPackge,

		.VisitedNodes = Params.VisitedNodes,
	};

	DependencyInfo& Dependencies = PackageInfos.at(Params.RequiredPackge).PackageDependencies;

	SingleDependencyIterationParamsInternal StructsParams{
		.CallbackForEachPackage = CallbackForEachPackage,
		.OnFoundCycle = OnFoundCycle,

		.NewParams = NewParams,
		.OldParams = Params,
		.Dependencies = Dependencies.StructsDependencies,
		.VisitedNodes = Params.VisitedNodes,

		.CurrentIndex = Params.RequiredPackge,
		.PrevIndex = Params.PrevPackage,
		.IterationHitCounterRef = Dependencies.StructsIterationHitCount,

		.bShouldHandlePackage = Params.bRequiresStructs,
		.bIsStruct = true,
	};

	SingleDependencyIterationParamsInternal ClassesParams{
		.CallbackForEachPackage = CallbackForEachPackage,
		.OnFoundCycle = OnFoundCycle,

		.NewParams = NewParams,
		.OldParams = Params,
		.Dependencies = Dependencies.ClassesDependencies,
		.VisitedNodes = Params.VisitedNodes,

		.CurrentIndex = Params.RequiredPackge,
		.PrevIndex = Params.PrevPackage,
		.IterationHitCounterRef = Dependencies.ClassesIterationHitCount,

		.bShouldHandlePackage = Params.bRequiresClasses,
		.bIsStruct = false,
	};

	IterateSingleDependencyImplementation(StructsParams, bCheckForCycle);
	IterateSingleDependencyImplementation(ClassesParams, bCheckForCycle);
}

void PackageManager::IterateDependencies(const IteratePackagesCallbackType& CallbackForEachPackage)
{
	VisitedNodeContainerType VisitedNodes;

	PackageManagerIterationParams Params = {
		.PrevPackage = -1,

		.VisitedNodes = VisitedNodes,
	};

	FindCycleCallbackType OnCycleFoundCallback = [](const PackageManagerIterationParams& OldParams, const PackageManagerIterationParams& NewParams, bool bIsStruct) -> void { };

	/* Increment hit counter for new iteration-cycle */
	CurrentIterationHitCount++;

	for (const auto& [PackageIndex, Info] : PackageInfos)
	{
		Params.RequiredPackge = PackageIndex;
		Params.bWasPrevNodeStructs = true;
		Params.bRequiresClasses = true;
		Params.bRequiresStructs = true;
		Params.VisitedNodes.clear();

		IterateDependenciesImplementation(Params, CallbackForEachPackage, OnCycleFoundCallback, false);
	}
}

void PackageManager::FindCycle(const FindCycleCallbackType& OnFoundCycle)
{
	VisitedNodeContainerType VisitedNodes;

	PackageManagerIterationParams Params = {
		.PrevPackage = -1,

		.VisitedNodes = VisitedNodes,
	};

	FindCycleCallbackType CallbackForEachPackage = [](const PackageManagerIterationParams& OldParams, const PackageManagerIterationParams& NewParams, bool bIsStruct) -> void {};

	/* Increment hit counter for new iteration-cycle */
	CurrentIterationHitCount++;

	for (const auto& [PackageIndex, Info] : PackageInfos)
	{
		Params.RequiredPackge = PackageIndex;
		Params.bWasPrevNodeStructs = true;
		Params.bRequiresClasses = true;
		Params.bRequiresStructs = true;
		Params.VisitedNodes.clear();

		IterateDependenciesImplementation(Params, CallbackForEachPackage, OnFoundCycle, true);
	}
}

