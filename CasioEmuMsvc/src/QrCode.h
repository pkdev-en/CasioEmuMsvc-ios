#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace casioemu {

	/**
	 * Holds the most recent QR payload the emulator has produced or captured.
	 * Casio calculators in this hardware family can export program/data
	 * transfers as a QR code (e.g. for scanning with a phone instead of
	 * cabling to a PC); this is the in-memory holder for that payload between
	 * the moment the chipset/UI generates it and the moment something (a UI
	 * window, a plugin, a test) reads it back out.
	 *
	 * Kept intentionally small: the field in Emulator (`qr_code`) is a plain
	 * value member, not a pointer, so this type stays cheap to default
	 * construct and copy.
	 */
	class QrCodeCapture {
	public:
		QrCodeCapture() = default;

		// Raw text/data payload encoded in the QR code (URL, base64 blob,
		// whatever the chipset's transfer protocol produces).
		void SetPayload(std::string payload) {
			std::lock_guard<std::mutex> lock(mutex_);
			payload_ = std::move(payload);
			has_payload_ = true;
		}

		std::string GetPayload() const {
			std::lock_guard<std::mutex> lock(mutex_);
			return payload_;
		}

		bool HasPayload() const {
			std::lock_guard<std::mutex> lock(mutex_);
			return has_payload_;
		}

		void Clear() {
			std::lock_guard<std::mutex> lock(mutex_);
			payload_.clear();
			has_payload_ = false;
		}

		// Rendered QR module grid, if a UI window wants to draw it directly
		// instead of re-encoding the payload itself. `size` is the side
		// length in modules (e.g. 25 for a 25x25 QR code); `modules` is a
		// flattened row-major array of size*size booleans packed as bytes
		// (1 = dark module, 0 = light module). Empty until something calls
		// SetRenderedGrid.
		void SetRenderedGrid(int size, std::vector<std::uint8_t> modules) {
			std::lock_guard<std::mutex> lock(mutex_);
			grid_size_ = size;
			grid_modules_ = std::move(modules);
		}

		int GetGridSize() const {
			std::lock_guard<std::mutex> lock(mutex_);
			return grid_size_;
		}

		std::vector<std::uint8_t> GetGridModules() const {
			std::lock_guard<std::mutex> lock(mutex_);
			return grid_modules_;
		}

	private:
		mutable std::mutex mutex_;
		std::string payload_;
		bool has_payload_ = false;
		int grid_size_ = 0;
		std::vector<std::uint8_t> grid_modules_;
	};

} // namespace casioemu
