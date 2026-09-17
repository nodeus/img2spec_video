// imgui_utils.h — ImGui slider helpers shared by modifier ui() implementations (Phase 3.4).
// Moved out of the Modifier base class so Modifier no longer defines UI widgets.
// Requires: imgui.h, stdio.h, and `gDirty` (declared extern here, defined in main.cpp).
#pragma once

extern int gDirty;

static int complexsliderfloat(char *aName, float *aValue, float aX0, float aX1, float aReset, float aNudge)
{
	char buttonstring[256];
	char resetstring[256];
	char plusstring[256];
	char minusstring[256];
	sprintf(buttonstring, "##%s", aName);
	sprintf(resetstring, "Reset##%s", aName);
	sprintf(minusstring, "-##%s", aName);
	sprintf(plusstring, "+##%s", aName);
	int modified = 0;

	if (ImGui::SliderFloat(buttonstring, aValue, aX0, aX1)) { modified = 1; }	ImGui::SameLine();
	if (ImGui::Button(minusstring) && *aValue - aNudge >= aX0) { modified = 1; *aValue -= aNudge; } ImGui::SameLine();
	if (ImGui::Button(plusstring) && *aValue + aNudge <= aX1) { modified = 1; *aValue += aNudge; } ImGui::SameLine();
	if (ImGui::Button(resetstring)) { modified = 1; *aValue = aReset; } ImGui::SameLine();
	ImGui::Text(aName);

	gDirty = gDirty | modified;
	
	return modified;
}

static int complexsliderint(char *aName, int *aValue, int aX0, int aX1, int aReset, int aNudge)
{
	char buttonstring[256];
	char resetstring[256];
	char plusstring[256];
	char minusstring[256];
	sprintf(buttonstring, "##%s", aName);
	sprintf(resetstring, "Reset##%s", aName);
	sprintf(minusstring, "-##%s", aName);
	sprintf(plusstring, "+##%s", aName);
	int modified = 0;

	if (ImGui::SliderInt(buttonstring, aValue, aX0, aX1)) { modified = 1; }	ImGui::SameLine();
	if (ImGui::Button(minusstring) && *aValue - aNudge >= aX0) { modified = 1; *aValue -= aNudge; } ImGui::SameLine();
	if (ImGui::Button(plusstring) && *aValue + aNudge <= aX1) { modified = 1; *aValue += aNudge; } ImGui::SameLine();
	if (ImGui::Button(resetstring)) { modified = 1; *aValue = aReset; } ImGui::SameLine();

	gDirty = gDirty | modified;

	ImGui::Text(aName);
	return modified;
}
