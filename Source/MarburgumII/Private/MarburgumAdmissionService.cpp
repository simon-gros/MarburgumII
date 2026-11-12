#include "MarburgumAdmissionService.h"

#include "Engine/World.h"

UMarburgumAdmissionService* UMarburgumAdmissionService::Get(const UWorld* World)
{
    if (!World)
    {
        return nullptr;
    }
    // Abstract base: the collection only ever holds the concrete server-module subclass.
    const TArray<UMarburgumAdmissionService*> Services = World->GetSubsystemArrayCopy<UMarburgumAdmissionService>();
    return Services.IsEmpty() ? nullptr : Services[0];
}
