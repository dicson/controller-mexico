#include "web_interface.h"
#include "config.h"
#include "hardware.h"

/**
 * @brief Функция построения веб-интерфейса.
 * Отображает состояние реле, статус полива, настройки зон и расписание.
 */
void build(sets::Builder &b)
{
    if (rtc_error)
        b.Label(H(alert), F("RTC"), alert_f, sets::Colors::Red);
    else
        b.Label(H(alert), F("RTC"), alert_f, sets::Colors::Green);

    b.Label(H(Time), F("Текущее время"), sett.rtc.toString());

    if (b.beginRow(F("Состояние реле"), sets::DivType::Block))
    {
        b.LED(H(relay1), F("реле 1"), RELAY_STATE[0]);
        b.LED(H(relay2), F("реле 2"), RELAY_STATE[1]);
        b.LED(H(relay3), F("реле 3"), RELAY_STATE[2]);
        b.endRow();
    }

    if (!watering_active)
    {
        if (db[kk::z1_on].toBool() || db[kk::z2_on].toBool() || db[kk::z3_on].toBool())
        {
            status = F("Ожидание расписания");
            b.Label(H(Status), F("Статус"), status, sets::Colors::Green);
        }
        else
        {
            status = F("Автополив выключен");
            b.Label(H(Status), F("Статус"), status, sets::Colors::Red);
        }
    }
    else
    {
        status = String(F("ПОЛИВ: Зона "));
        status += (current_zone + 1);
        b.Label(H(Status), F("Статус"), status);
    }

    // БЛОК РУЧНОГО ЗАПУСКА ВСЕЙ ЦЕПОЧКИ
    if (!watering_active)
    {
        if (b.Button(H(Button), F("Запустить полив сейчас"), sets::Colors::Green))
            startWateringSequence();
    }
    else
    {
        if (b.Button(H(Button), F("ОСТАНОВИТЬ ВСЁ"), sets::Colors::Red))
        {
            Serial.println(F("Принудительная остановка всей очереди"));
            stopAction();
            endTimeToLog();
        }
    }

    // НАСТРОЙКИ ЗОН (Выключатель + Ползунок рядом)
    if (b.beginGroup(F("Настройка Зон Полива")))
    {
        if (b.beginRow(F("🌱 Зона 1"), sets::DivType::Default))
        {
            if (b.Switch(kk::z1_on, F("Поливать")))
                updateHoldStatus();
            b.Spinner(kk::dur_1, F("(минут)"), 0, 60, 1);
            b.endRow();
        }
        if (b.beginRow(F("🌱 Зона 2"), sets::DivType::Block))
        {
            if (b.Switch(kk::z2_on, F("Поливать")))
                updateHoldStatus();
            b.Spinner(kk::dur_2, F("(минут)"), 0, 60, 1);
            b.endRow();
        }
        if (b.beginRow(F("🌱 Зона 3"), sets::DivType::Block))
        {
            if (b.Switch(kk::z3_on, F("Поливать")))
                updateHoldStatus();
            b.Spinner(kk::dur_3, F("(минут)"), 0, 60, 1);
            b.endRow();
        }
        b.endGroup();
    }

    if (b.beginMenu(F("⏰ Расписание полива")))
    {
        if (b.beginGroup(F("Дни полива")))
        {
            b.Switch(kk::d_1, F("Понедельник"));
            b.Switch(kk::d_2, F("Вторник"));
            b.Switch(kk::d_3, F("Среда"));
            b.Switch(kk::d_4, F("Четверг"));
            b.Switch(kk::d_5, F("Пятница"));
            b.Switch(kk::d_6, F("Суббота"));
            b.Switch(kk::d_7, F("Воскресенье"));
            b.endGroup();
        }

        if (b.beginGroup(F("Время старта полива")))
        {
            b.Slider(kk::tm_hour, F("Час старта"), 0, 23, 1);
            b.Slider(kk::tm_min, F("Минута старта"), 0, 59, 1);
            b.endGroup();
        }
        if (b.beginMenu(F("Ручное управление")))
        {
            if (b.enterMenu())
            {
                if (watering_active)
                {
                    stopAction();
                    sett.updater().update(H(switch1), RELAY_STATE[0]).update(H(switch2), RELAY_STATE[1]).update(H(switch3), RELAY_STATE[2]).update(H(switch4), RELAY_STATE[3]);
                    endTimeToLog();
                }
                updateLogger();
            }

            if (b.Switch(H(switch1), F("Зона 1"), &RELAY_STATE[0]))
                switchRelay(0);
            if (b.Switch(H(switch2), F("Зона 2"), &RELAY_STATE[1]))
                switchRelay(1);
            if (b.Switch(H(switch3), F("Зона 3"), &RELAY_STATE[2]))
                switchRelay(2);

            b.Paragraph(F("  ВНИМАНИЕ❗"), F("При входе в это меню, выполняемая в данный момент программа прерывается!"));

            // логгер
            b.Log(H(log), logger, F("Журнал запусков полива"));

            b.endMenu(); // Ручное управление
        }
        b.endMenu(); // Расписание полива
    }
} // build()

/**
 * @brief Callback-функция для синхронизации времени RTC.
 * @param unix_time Время в формате UNIX.
 */
void onSyncCallback(uint32_t unix_time)
{
    rtc.setUnix(unix_time);
}

/**
 * @brief Обновляет виджеты веб-интерфейса (реле, кнопки, статус RTC, логи).
 */
void updateWidgets()
{
    if (!sett.focused())
        return;
    for (int i = 0; i < 3; i++)
    {
        sett.updater().update(LED_NAMES[i], RELAY_STATE[i]);
    }
    if (!watering_active)
    {
        // clang-format off
        sett.updater()
            .updateColor(H(Button), sets::Colors::Green)
            .updateText(H(Button), F("Запустить полив сейчас"));
    }
    else
    {
        sett.updater()
            .updateColor(H(Button), sets::Colors::Red)
            .updateText(H(Button), F("ОСТАНОВИТЬ ВСЁ"));
        // clang-format on
    }

    if (!rtc_error)
    {
        char buf[40];
        sprintf(buf, "RTC в норме %d °C", rtc.getTempInt());
        alert_f = buf;
        sett.updater().updateText(H(alert), alert_f);
    }

    updateHoldStatus();
    updateLogger();
}

/**
 * @brief Обновляет статус "Автополив" в веб-интерфейсе в зависимости от того, активен он или нет.
 */
void updateHoldStatus()
{
    if (!watering_active)
    {
        auto u = sett.updater();
        if (db[kk::z1_on].toBool() || db[kk::z2_on].toBool() || db[kk::z3_on].toBool())
        {
            status = "Ожидание расписания";
            u.updateColor(H(Status), sets::Colors::Green);
        }
        else
        {
            status = "Автополив выключен";
            u.updateColor(H(Status), sets::Colors::Red);
        }
        u.updateText(H(Status), status);
    }
}

/**
 * @brief Обновляет статус полива в реальном времени, показывая оставшееся время.
 */
void updateStatus()
{
    if (!sett.focused())
        return;
    uint32_t elapsed = millis() - zone_start_millis;
    uint32_t limit = (uint32_t)zone_durations[current_zone] * 60 * 1000;
    if (limit > elapsed)
    {
        uint32_t passed = limit - elapsed;
        uint32_t allSeconds = passed / 1000;
        char buf[40];
        snprintf(buf, sizeof(buf), "ПОЛИВ: Зона %d (%02d:%02d)", current_zone + 1, (allSeconds / 60) % 60, allSeconds % 60);
        String new_status = buf;
        if (new_status != status)
        {
            status = new_status;
            sett.updater().updateText(H(Status), status);
        }
    }
}

/**
 * @brief Обновляет данные журнала на веб-интерфейсе, считывая записи из базы данных.
 */
void updateLogger()
{
    logger.clear();
    for (int i = 0; i < 15; i++)
    {
        String log_entry = db[kk::log_0 + i].toString();
        if (log_entry.length() > 0)
        {
            logger.println(log_entry);
        }
    }
    sett.updater().update(H(log), logger);
}

/**
 * @brief Добавляет новую запись в журнал поливов в базе данных.
 * @param entry Текст записи журнала.
 */
void addLog(String entry)
{
    // Shift logs: log_13 = log_12, ..., log_1 = log_0
    for (int i = 14; i > 0; i--)
    {
        db[kk::log_0 + i] = db[kk::log_0 + i - 1].toString();
    }
    // New log at log_0
    db[kk::log_0] = entry;

    updateLogger();
}

/**
 * @brief Добавляет время окончания полива в последнюю запись журнала.
 */
void endTimeToLog()
{
    String entry = db[kk::log_0].toString() + " - " + sett.rtc.timeToString();
    db[kk::log_0] = entry;
    updateLogger();
}
