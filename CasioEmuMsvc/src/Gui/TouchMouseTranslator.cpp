#include "TouchMouseTranslator.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T
//  ���� / ����
// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T

TouchMouseTranslator::TouchMouseTranslator(
	Uint32 windowId,
	EventSink sink,
	GuiHitTest guiHitTest)
	: windowId_(windowId),
	  sink_(std::move(sink)),
	  guiHitTest_(std::move(guiHitTest)) {
}

void TouchMouseTranslator::SetWindowId(Uint32 windowId) {
	windowId_ = windowId;
}

bool TouchMouseTranslator::HandleEvent(const SDL_Event& event,
	int windowW, int windowH) {
	switch (event.type) {
	case SDL_FINGERDOWN:
		return HandleFingerDown(event.tfinger, windowW, windowH);
	case SDL_FINGERUP:
		return HandleFingerUp(event.tfinger, windowW, windowH);
	case SDL_FINGERMOTION:
		return HandleFingerMotion(event.tfinger, windowW, windowH);
	default:
		return false;
	}
}

// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T
//  FingerDown
// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T

bool TouchMouseTranslator::HandleFingerDown(
	const SDL_TouchFingerEvent& finger, int windowW, int windowH) {

	const float x = finger.x * static_cast<float>(windowW);
	const float y = finger.y * static_cast<float>(windowH);

	if (!primary_.active) {
		const bool hitImGui =
			guiHitTest_ ? guiHitTest_(x, y) : false;
		const TouchTarget target =
			hitImGui ? TouchTarget::ImGui : TouchTarget::Emulator;

		StartFinger(primary_, finger.fingerId, x, y, target);

		/*
		 * ���� ImGui ���� Emulator�������� FingerDown ʱ����������ꡣ
		 * �� Motion ������ֵ��ʼ��ק������ Up ʱ�ж� tap / long-press��
		 * �����ƶ����벻���Ͳ��ᴥ���κ�����¼���
		 */
		return true;
	}

	if (!secondary_.active && finger.fingerId != primary_.fingerId) {
		// �ڶ�����ָ���� �� ת˫ָ����
		// ���ͷ���������⿨ס
		if (primary_.dragging || leftButtonDown_) {
			EmitMouseButton(primary_.target, SDL_BUTTON_LEFT,
				SDL_RELEASED,
				primary_.currentX, primary_.currentY);
			primary_.dragging = false;
		}

		StartFinger(secondary_, finger.fingerId, x, y, primary_.target);
		secondary_.suppressTap = true;
		primary_.suppressTap = true;
		return true;
	}

	return true;
}

// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T
//  FingerUp
// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T

bool TouchMouseTranslator::HandleFingerUp(
	const SDL_TouchFingerEvent& finger, int windowW, int windowH) {

	const float x = finger.x * static_cast<float>(windowW);
	const float y = finger.y * static_cast<float>(windowH);
	const Uint32 now = SDL_GetTicks();

	// FIX: re-check the hit-test target at FingerUp time (not just at
	// FingerDown). A modal/popup (e.g. an "OK" dialog) can open in the
	// same frame the finger went down, in which case the original
	// FingerDown hit-test still sees the *previous* frame's layout and
	// routes the tap to the Emulator (calculator keypad) instead of
	// ImGui. Re-testing immediately before the tap/click is emitted
	// fixes taps on buttons inside dialogs that just appeared.
	if (guiHitTest_) {
		if (primary_.active && primary_.fingerId == finger.fingerId && !primary_.dragging && !leftButtonDown_)
			primary_.target = guiHitTest_(x, y) ? TouchTarget::ImGui : TouchTarget::Emulator;
		else if (secondary_.active && secondary_.fingerId == finger.fingerId && !secondary_.dragging && !leftButtonDown_)
			secondary_.target = guiHitTest_(x, y) ? TouchTarget::ImGui : TouchTarget::Emulator;
	}

	// ���� primary up ������������������������������������������������������������������������������������
	if (primary_.active && primary_.fingerId == finger.fingerId) {
		primary_.currentX = x;
		primary_.currentY = y;

		if (primary_.dragging || leftButtonDown_) {
			// ������ק�� �� �ͷ����
			EmitMouseMotion(primary_.target, x, y);
			EmitMouseButton(primary_.target, SDL_BUTTON_LEFT,
				SDL_RELEASED, x, y);
			primary_.dragging = false;
		}
		else {
			// û����ק �� �ж� tap / long-press
			const float dx = x - primary_.startX;
			const float dy = y - primary_.startY;
			const float distSq = dx * dx + dy * dy;
			const float thresholdSq =
				dragThresholdPixels_ * dragThresholdPixels_;

			if (primary_.isTapCandidate && distSq <= thresholdSq) {
				if (now - primary_.startTime < longPressDelayMs_) {
					// �̰� �� ������
					EmitMouseClick(primary_.target,
						SDL_BUTTON_LEFT, x, y);
				}
				else {
					// ���� �� �Ҽ����
					EmitMouseClick(primary_.target,
						SDL_BUTTON_RIGHT, x, y);
				}
			}
			// �ƶ����벻�� �� suppressTap �� ʲô������
		}

		ResetFinger(primary_);
		if (secondary_.active) {
			PromoteSecondFingerToPrimary();
		}
		return true;
	}

	// ���� secondary up ��������������������������������������������������������������������������������
	if (secondary_.active && secondary_.fingerId == finger.fingerId) {
		secondary_.currentX = x;
		secondary_.currentY = y;

		if (secondary_.dragging || leftButtonDown_) {
			EmitMouseButton(secondary_.target, SDL_BUTTON_LEFT,
				SDL_RELEASED, x, y);
			secondary_.dragging = false;
		}
		ResetFinger(secondary_);
		primary_.suppressTap = true;
		return true;
	}

	return true;
}

// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T
//  FingerMotion
// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T

bool TouchMouseTranslator::HandleFingerMotion(
	const SDL_TouchFingerEvent& finger, int windowW, int windowH) {

	const float x = finger.x * static_cast<float>(windowW);
	const float y = finger.y * static_cast<float>(windowH);

	// ���� primary motion ����������������������������������������������������������������������������
	if (primary_.active && primary_.fingerId == finger.fingerId) {

		if (!secondary_.active) {
			HandleSingleFingerMove(primary_, x, y);
		}
		else {
			primary_.suppressTap = true;
			secondary_.suppressTap = true;
			HandleTwoFingerMove(primary_, x, y,
				primary_.currentX, primary_.currentY);
		}

		primary_.currentX = x;
		primary_.currentY = y;
		AddTrail(primaryTrail_, x, y);
		return true;
	}

	// ���� secondary motion ������������������������������������������������������������������������
	if (secondary_.active && secondary_.fingerId == finger.fingerId) {

		if (primary_.active) {
			primary_.suppressTap = true;
			secondary_.suppressTap = true;
			HandleTwoFingerMove(secondary_, x, y,
				primary_.currentX, primary_.currentY);
		}

		secondary_.currentX = x;
		secondary_.currentY = y;
		AddTrail(secondaryTrail_, x, y);
		return true;
	}

	return true;
}

// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T
//  �ڲ��ƶ�����
// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T

void TouchMouseTranslator::HandleSingleFingerMove(
	TouchState& state, float x, float y) {

	const float dx = x - state.startX;
	const float dy = y - state.startY;
	const float distSq = dx * dx + dy * dy;
	const float thresholdSq = dragThresholdPixels_ * dragThresholdPixels_;
	
	if (std::abs(dx) > 3.0f || std::abs(dy) > 3.0f)
		state.isTapCandidate = false;
	
	if (distSq > thresholdSq)
	{
		state.movedBeyondThreshold = true;
	}

	if (!state.dragging && distSq > thresholdSq){
	state.dragging = true;
	state.suppressTap = true;

	EmitMouseMotion(state.target, x, y);
	EmitMouseButton(state.target, SDL_BUTTON_LEFT, SDL_PRESSED, x, y);
}

	if (state.dragging) {
		EmitMouseMotion(state.target, x, y);
	}
}

void TouchMouseTranslator::HandleTwoFingerMove(
	TouchState& state, float x, float y,
	float anchorX, float anchorY)
{
	float moveY = y - state.currentY;

	Uint32 now = SDL_GetTicks();

	state.scrollAccum += moveY;
	
	if (std::abs(state.scrollAccum) < 2.0f)
		return;

	EmitMouseWheel(state.target,
				   state.scrollAccum,
				   anchorX, anchorY);

	state.scrollAccum = 0;
	state.lastScrollTime = now;
}

// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T
//  ����
// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T

void TouchMouseTranslator::StartFinger(
	TouchState& state, SDL_FingerID fingerId,
	float x, float y, TouchTarget target) {

	state.active = true;
	state.dragging = false;
	state.suppressTap = false;
	state.isTapCandidate = true;
	state.movedBeyondThreshold = false;
	state.scrollAccum = 0;
	state.lastScrollTime = 0;
	state.fingerId = fingerId;
	state.startX = x;
	state.startY = y;
	state.currentX = x;
	state.currentY = y;
	state.startTime = SDL_GetTicks();
	state.target = target;

	if (&state == &primary_) {
		ResetTrail(primaryTrail_);
	}
	else {
		ResetTrail(secondaryTrail_);
	}
}

void TouchMouseTranslator::ResetFinger(TouchState& state) {
	state = TouchState{};
}

void TouchMouseTranslator::PromoteSecondFingerToPrimary() {
	primary_ = secondary_;
	primary_.startX = primary_.currentX;
	primary_.startY = primary_.currentY;
	primary_.startTime = SDL_GetTicks();
	primary_.dragging = false;
	primary_.suppressTap = true;
	ResetFinger(secondary_);
}

void TouchMouseTranslator::AddTrail(TouchTrail& trail, float x, float y) {
	trail.samples[trail.currentIndex] =
		TouchSample{x, y, SDL_GetTicks()};
	trail.currentIndex =
		(trail.currentIndex + 1) % TrailBufferSize;
	trail.count =
		std::min<std::size_t>(trail.count + 1, TrailBufferSize);
}

void TouchMouseTranslator::ResetTrail(TouchTrail& trail) {
	trail.currentIndex = 0;
	trail.count = 0;
}

// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T
//  ��Ⱦ
// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T

static void RenderFilledCircle(SDL_Renderer* renderer,
	int cx, int cy, int radius) {
	for (int dy = -radius; dy <= radius; ++dy) {
		int dx = static_cast<int>(
			std::sqrt(radius * radius - dy * dy));
		SDL_RenderDrawLine(renderer,
			cx - dx, cy + dy,
			cx + dx, cy + dy);
	}
}

static void RenderInterpolatedCircleLine(
	SDL_Renderer* renderer,
	float x1, float y1,
	float x2, float y2,
	float radius) {

	float dx = x2 - x1;
	float dy = y2 - y1;
	float dist = std::sqrt(dx * dx + dy * dy);
	int steps = std::max(1, static_cast<int>(dist / 1.5f));

	for (int i = 0; i <= steps; ++i) {
		float t = static_cast<float>(i) / static_cast<float>(steps);
		float x = x1 + dx * t;
		float y = y1 + dy * t;
		RenderFilledCircle(renderer,
			static_cast<int>(x),
			static_cast<int>(y),
			static_cast<int>(radius));
	}
}

void TouchMouseTranslator::RenderDebug(SDL_Renderer* renderer) const {
	if (!renderer) {
		return;
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	DrawTrail(renderer, primaryTrail_);
	DrawTrail(renderer, secondaryTrail_);

	DrawCross(renderer, primary_, 255, 0, 0);
	DrawCross(renderer, secondary_, 0, 255, 0);

	// �������Ȼ���������ָ��ס��δ��ק��δ suppress ʱ��ʾ
	DrawLongPressRing(renderer, primary_);
	DrawLongPressRing(renderer, secondary_);
}

void TouchMouseTranslator::DrawTrail(SDL_Renderer* renderer,
	const TouchTrail& trail) const {
	const Uint32 now = SDL_GetTicks();

	if (trail.count < 2) {
		return;
	}

	for (std::size_t i = 0; i + 1 < trail.count; ++i) {
		const std::size_t idx1 =
			(trail.currentIndex + TrailBufferSize - 1 - i) % TrailBufferSize;
		const std::size_t idx2 =
			(trail.currentIndex + TrailBufferSize - 2 - i) % TrailBufferSize;

		const TouchSample& s1 = trail.samples[idx1];
		const TouchSample& s2 = trail.samples[idx2];

		Uint32 age = now - s1.time;
		if (age > trailDurationMs_) {
			continue;
		}

		float t = static_cast<float>(age) / static_cast<float>(trailDurationMs_);
		float radius = 2.0f + 8.0f * (1.0f - t);
		Uint8 alpha = static_cast<Uint8>(80.0f * (1.0f - t));

		SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
		RenderInterpolatedCircleLine(renderer,
			s1.x, s1.y,
			s2.x, s2.y,
			radius);
	}
}

void TouchMouseTranslator::DrawCross(SDL_Renderer* renderer,
	const TouchState& state,
	Uint8 r, Uint8 g, Uint8 b) const {
	if (!state.active) {
		return;
	}
	// ��ѡ������ʮ��׼��
	// SDL_SetRenderDrawColor(renderer, r, g, b, 255);
	// ...
	(void)r;
	(void)g;
	(void)b;
}

void TouchMouseTranslator::DrawLongPressRing(
	SDL_Renderer* renderer,
	const TouchState& state) const {

	if (!state.active) {
		return;
	}

	// ������ק���ѱ����� �� ����ʾ��
	if (state.dragging || state.suppressTap) {
		return;
	}

	// ����ƶ������Ƿ�����ֵ��
	const float dx = state.currentX - state.startX;
	const float dy = state.currentY - state.startY;
	const float distSq = dx * dx + dy * dy;
	const float thresholdSq =
		dragThresholdPixels_ * dragThresholdPixels_;
	if (distSq > thresholdSq) {
		return;
	}

	const Uint32 now = SDL_GetTicks();
	const Uint32 elapsed = now - state.startTime;

	// ���� 0.0 ~ 1.0
	float progress = static_cast<float>(elapsed) / static_cast<float>(longPressDelayMs_);
	progress = std::min(progress, 1.0f);

	// ����̫С����
	if (progress < 0.5f) {
		return;
	}
	progress = (progress - 0.5f) * 2.0f; // 0.5~1.0 ӳ�䵽 0.0~1.0

	const int cx = static_cast<int>(state.startX);
	const int cy = static_cast<int>(state.startY);
	const float endAngle = progress * 2.0f * static_cast<float>(M_PI);

	// ��ɫ��δ����ɫ��������ɫ
	if (progress >= 1.0f) {
		SDL_SetRenderDrawColor(renderer, 100, 255, 100, 220);
	}
	else {
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
	}

	// ���߶αƽ�Բ��
	const int segments = std::max(16, static_cast<int>(progress * 64));
	const float startAng = -static_cast<float>(M_PI) / 2.0f; // 12 ���ӷ���

	for (int i = 0; i < segments; ++i) {
		float a1 = startAng + endAngle * static_cast<float>(i) / static_cast<float>(segments);
		float a2 = startAng + endAngle * static_cast<float>(i + 1) / static_cast<float>(segments);

		// ��ÿ�λ��ߣ��������ģ���߿�
		for (float r = ringRadius_ - ringThickness_ * 0.5f;
			r <= ringRadius_ + ringThickness_ * 0.5f;
			r += 1.0f) {

			int x1 = cx + static_cast<int>(r * std::cos(a1));
			int y1 = cy + static_cast<int>(r * std::sin(a1));
			int x2 = cx + static_cast<int>(r * std::cos(a2));
			int y2 = cy + static_cast<int>(r * std::sin(a2));

			SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
		}
	}

	// ����֮��һ��С��ǿ��
	if (progress >= 1.0f) {
		SDL_SetRenderDrawColor(renderer, 100, 255, 100, 255);
		RenderFilledCircle(renderer,
			cx + static_cast<int>(
					 ringRadius_ * std::cos(startAng + endAngle)),
			cy + static_cast<int>(
					 ringRadius_ * std::sin(startAng + endAngle)),
			static_cast<int>(ringThickness_));
	}
}

// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T
//  �¼�����
// �T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T�T

void TouchMouseTranslator::EmitMouseMotion(
	TouchTarget target, float x, float y) {

	if (!sink_)
		return;

	SDL_Event event{};
	event.type = SDL_MOUSEMOTION;
	event.motion.timestamp = SDL_GetTicks();
	event.motion.windowID = windowId_;
	event.motion.which = SDL_TOUCH_MOUSEID;
	event.motion.state = leftButtonDown_ ? SDL_BUTTON_LMASK : 0;
	event.motion.x = static_cast<Sint32>(std::lround(x));
	event.motion.y = static_cast<Sint32>(std::lround(y));
	event.motion.xrel = 0;
	event.motion.yrel = 0;

	sink_(event, target);
}

void TouchMouseTranslator::EmitMouseButton(
	TouchTarget target, Uint8 button, Uint8 state,
	float x, float y) {

	if (!sink_)
		return;

	SDL_Event event{};
	event.type = (state == SDL_PRESSED)
					 ? SDL_MOUSEBUTTONDOWN
					 : SDL_MOUSEBUTTONUP;
	event.button.timestamp = SDL_GetTicks();
	event.button.windowID = windowId_;
	event.button.which = SDL_TOUCH_MOUSEID;
	event.button.button = button;
	event.button.state = state;
	event.button.clicks = 1;
	event.button.x = static_cast<Sint32>(std::lround(x));
	event.button.y = static_cast<Sint32>(std::lround(y));

	sink_(event, target);

	if (button == SDL_BUTTON_LEFT) {
		leftButtonDown_ = (state == SDL_PRESSED);
	}
}

void TouchMouseTranslator::EmitMouseClick(
	TouchTarget target, Uint8 button, float x, float y) {

	EmitMouseMotion(target, x, y);
	EmitMouseButton(target, button, SDL_PRESSED, x, y);
	EmitMouseButton(target, button, SDL_RELEASED, x, y);
}

void TouchMouseTranslator::EmitMouseWheel(
	TouchTarget target, float deltaPixels,
	float mouseX, float mouseY) {

	if (!sink_)
		return;

	const float preciseY = deltaPixels / scrollPixelsPerWheel_;

	SDL_Event event{};
	event.type = SDL_MOUSEWHEEL;
	event.wheel.timestamp = SDL_GetTicks();
	event.wheel.windowID = windowId_;
	event.wheel.which = SDL_TOUCH_MOUSEID;
	event.wheel.x = 0;
	event.wheel.y = static_cast<Sint32>(std::lround(preciseY));
	event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;

#if SDL_VERSION_ATLEAST(2, 0, 18)
	event.wheel.preciseX = 0.0f;
	event.wheel.preciseY = preciseY;
#endif
#if SDL_VERSION_ATLEAST(2, 26, 0)
	event.wheel.mouseX = static_cast<Sint32>(std::lround(mouseX));
	event.wheel.mouseY = static_cast<Sint32>(std::lround(mouseY));
#endif

	sink_(event, target);
}
