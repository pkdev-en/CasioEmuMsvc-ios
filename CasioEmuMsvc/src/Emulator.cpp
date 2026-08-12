#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace casioemu {

	/**
	 * Holds model resources (config.bin, ROM images, interface graphics, and
	 * anything else normally read from the model's directory on disk) in
	 * memory instead. Used on platforms where the model is packaged directly
	 * into the app bundle rather than living at a filesystem path -- iOS and
	 * Android app sandboxes don't expose an arbitrary "model directory" the
	 * way desktop does, so the model gets loaded once into this store and
	 * read back by name instead of by path.
	 *
	 * Emulator::model_resources is null for the normal disk-backed path
	 * (Emulator::IsMemoryModel() returns false, GetModelFilePath() resolves
	 * a real filesystem path). When model_resources is set, Emulator reads
	 * through HasModelResource / ReadModelResource / WriteModelSessionResource
	 * instead.
	 */
	class ModelResourceStore {
	public:
		ModelResourceStore() = default;

		// Load a resource into the store under `name` (e.g. "config.bin",
		// "rom.bin", "interface.png"). Overwrites any existing entry with the
		// same name.
		void AddResource(const std::string& name, std::vector<std::uint8_t> data) {
			std::lock_guard<std::mutex> lock(mutex_);
			resources_[name] = std::move(data);
		}

		// True if a resource with this name was loaded into the store.
		bool HasResource(const std::string& name) const {
			std::lock_guard<std::mutex> lock(mutex_);
			return resources_.find(name) != resources_.end();
		}

		// Raw bytes for a resource. Returns an empty vector if the name isn't
		// present -- callers that need to distinguish "empty file" from
		// "missing" should check HasResource() first.
		std::vector<std::uint8_t> ReadResource(const std::string& name) const {
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = resources_.find(name);
			if (it == resources_.end())
				return {};
			return it->second;
		}

		// Session-only writes (e.g. save states, snapshot exports) that
		// shouldn't be persisted back into the packaged model. Stored
		// separately from the read-only base resources so a fresh
		// ReadResource() of a base file is never shadowed unless the caller
		// explicitly asks for the session copy via ReadSessionResource().
		void WriteSessionResource(const std::string& name, const std::vector<std::uint8_t>& data) {
			std::lock_guard<std::mutex> lock(mutex_);
			session_resources_[name] = data;
		}

		bool HasSessionResource(const std::string& name) const {
			std::lock_guard<std::mutex> lock(mutex_);
			return session_resources_.find(name) != session_resources_.end();
		}

		std::vector<std::uint8_t> ReadSessionResource(const std::string& name) const {
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = session_resources_.find(name);
			if (it == session_resources_.end())
				return {};
			return it->second;
		}

		size_t ResourceCount() const {
			std::lock_guard<std::mutex> lock(mutex_);
			return resources_.size();
		}

	private:
		mutable std::mutex mutex_;
		std::map<std::string, std::vector<std::uint8_t>> resources_;
		std::map<std::string, std::vector<std::uint8_t>> session_resources_;
	};

} // namespace casioemu
