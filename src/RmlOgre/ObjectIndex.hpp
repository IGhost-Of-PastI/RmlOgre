// ObjectIndex.hpp
#ifndef NIMBLE_RMLOGRE_OBJECTINDEX_HPP
#define NIMBLE_RMLOGRE_OBJECTINDEX_HPP

#include <optional>
#include <stdexcept>
#include <vector>

namespace nimble::RmlOgre {

	/**
	 * @brief Плотный контейнер со стабильными целочисленными индексами-хэндлами и переиспользованием слотов (free-list).
	 *
	 * Используется в @ref RenderInterface для сопоставления `Rml::TextureHandle`,
	 * `Rml::CompiledFilterHandle` и `Rml::CompiledShaderHandle` с ресурсами Ogre за $O(1)$.
	 *
	 * @warning Индексы стабильны, но адреса элементов (`&index[i]`) могут инвалидироваться
	 *          при вставке новых элементов из-за реаллокации `std::vector`.
	 */
	template <class T>
	class ObjectIndex
	{
		std::vector<std::optional<T>> array;
		std::vector<std::size_t> empty;

	public:
		/// Вставляет ресурс в свободный слот (или в конец) и возвращает его целочисленный хэндл.
		std::size_t insert(T&& resource)
		{
			std::size_t index;
			if (!this->empty.empty())
			{
				index = this->empty.back();
				this->empty.pop_back();
			}
			else
			{
				index = this->array.size();
				this->array.push_back(std::nullopt);
			}

			this->array[index] = std::move(resource);
			return index;
		}

		/// Копирует ресурс в свободный слот и возвращает его целочисленный хэндл.
		std::size_t insert(const T& resource)
		{
			return this->insert(T{ resource });
		}

		/// Освобождает слот и возвращает его индекс в free-list (повторное удаление безопасно).
		void erase(std::size_t index)
		{
			if (!this->has(index))
				return;

			this->array[index] = std::nullopt;
			this->empty.push_back(index);
		}

		/// Проверяет, что индекс в границах массива и слот занят.
		bool has(std::size_t index) const
		{
			return index < this->array.size()
				&& this->array[index].has_value();
		}

		/// Быстрый доступ без проверки границ (предполагается `has(index) == true`).
		T& operator[](std::size_t index)
		{
			return *this->array[index];
		}

		/// Константный доступ без проверки границ.
		const T& operator[](std::size_t index) const
		{
			return *this->array[index];
		}

		/// Доступ с проверкой наличия слота (бросает `std::out_of_range` при невалидном хэндле).
		T& at(std::size_t index)
		{
			if (!this->has(index))
				throw std::out_of_range("ObjectIndex::at index out of range");

			return this->array[index].value();
		}

		/// Константный доступ с проверкой наличия слота.
		const T& at(std::size_t index) const
		{
			if (!this->has(index))
				throw std::out_of_range("ObjectIndex::at index out of range");

			return this->array[index].value();
		}
	};

} // namespace nimble::RmlOgre

#endif // NIMBLE_RMLOGRE_OBJECTINDEX_HPP