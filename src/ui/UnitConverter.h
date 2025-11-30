/**
 * UnitConverter.h - Unit conversion for display values
 *
 * Provides app-wide unit selection (nm/um/mm) with conversion utilities.
 * Internal values are stored in micrometers, converted for display.
 */

#ifndef CHIPLET_UI_UNITCONVERTER_H
#define CHIPLET_UI_UNITCONVERTER_H

#include <QString>
#include <QStringList>

namespace chiplet {

/**
 * Display unit for dimensional values
 */
enum class DisplayUnit {
    Nanometers,
    Micrometers,
    Millimeters
};

/**
 * Singleton class for app-wide unit conversion
 *
 * Internal storage is always in micrometers (um).
 * This class handles conversion to/from display units.
 */
class UnitConverter {
public:
    /**
     * Get singleton instance
     */
    static UnitConverter& instance();

    /**
     * Current display unit
     */
    DisplayUnit currentUnit() const { return m_unit; }

    /**
     * Set display unit
     */
    void setUnit(DisplayUnit unit) { m_unit = unit; }

    /**
     * Convert from micrometers (internal) to current display unit
     */
    double toDisplay(double um) const;

    /**
     * Convert from current display unit to micrometers (internal)
     */
    double fromDisplay(double value) const;

    /**
     * Format value for display with unit suffix
     * @param um Value in micrometers
     * @param precision Decimal places (default 2)
     * @return Formatted string like "1234.56 um"
     */
    QString toDisplayString(double um, int precision = 2) const;

    /**
     * Get unit suffix string ("nm", "um", "mm")
     */
    QString unitSuffix() const;

    /**
     * Get list of available unit names for UI
     */
    static QStringList availableUnits();

    /**
     * Convert combo box index to DisplayUnit
     */
    static DisplayUnit unitFromIndex(int index);

    /**
     * Convert DisplayUnit to combo box index
     */
    static int indexFromUnit(DisplayUnit unit);

private:
    UnitConverter() = default;
    UnitConverter(const UnitConverter&) = delete;
    UnitConverter& operator=(const UnitConverter&) = delete;

    DisplayUnit m_unit = DisplayUnit::Micrometers;  // Default
};

} // namespace chiplet

#endif // CHIPLET_UI_UNITCONVERTER_H
