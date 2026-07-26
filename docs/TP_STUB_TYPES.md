# tp_stub — Типы и API

> Справочник по типам TJS2/KiriKiri, используемым в плагинах.

## Основные типы TJS2

| Тип | Описание | Использование |
|------|----------|--------------|
| `tjs_int` | 32-битное целое | Индексы, счётчики, флаги |
| `tTVInteger` | 64-битное целое | Числовые значения TJS |
| `tjs_real` | double | Числа с плавающей точкой |
| `tTVReal` | double | Вещественные значения TJS |
| `tjs_char` | wchar_t (UTF-16) | Символ TJS |
| `ttstr` | Строка TJS (надстройка над `tTJSString`) | Работа со строками |
| `tTJSVariant` | Универсальный тип TJS (может хранить int/real/string/object) | Передача значений между C++ и TJS |
| `tTJSVariantType` | enum (`tvtVoid`, `tvtObject`, `tvtString`, `tvtInteger`, `tvtReal`) | Определение типа Variant |
| `iTJSDispatch2` | Интерфейс TJS-объекта | Доступ к свойствам/методам объекта |
| `tTJSDispatch` | Базовый класс для TJS-диспатча | Реализация TJS-объектов в C++ |
| `tTJSVariantClosure` | Пара `(объект, objthis)` для callback | Калбеки TJS |

## Сигнатура TJS функции (raw callback)

```cpp
tjs_error TJS_INTF_METHOD FunctionName(
    tTJSVariant* result,    // [out] возвращаемое значение (может быть null)
    tjs_int numparams,       // количество переданных параметров
    tTJSVariant** param,     // массив параметров
    iTJSDispatch2* objthis   // контекст вызова (this)
);
```

## ncbind — макросы регистрации

| Макрос | Назначение |
|--------|-----------|
| `NCB_REGISTER_FUNCTION(name, function)` | Глобальная функция TJS |
| `NCB_REGISTER_CLASS(Name)` | Класс TJS |
| `NCB_REGISTER_CLASS_DIFFER(Name, Class)` | Класс с другим именем |
| `NCB_ATTACH_FUNCTION(name, attach, function)` | Функция, прикреплённая к классу |
| `NCB_ATTACH_CLASS(Class, TJS2Class)` | Прикрепить C++ класс к существующему TJS классу |
| `NCB_CONSTRUCTOR((args))` | Конструктор класса |
| `NCB_METHOD(name)` | Метод класса |
| `NCB_PROPERTY(name, getter, setter)` | Свойство класса |
| `NCB_PROPERTY_RO(name, getter)` | Свойство только для чтения |

## ncbind — типы параметров

| Тип C++ | TJS тип | Примечание |
|---------|---------|-----------|
| `int` | Integer | Автоматически конвертируется |
| `tjs_int` | Integer | |
| `tTVInteger` | Integer | |
| `double` | Real | |
| `char const*` | String (UTF-8) | Автоконвертация |
| `const tjs_char*` | String (UTF-16) | Нативный TJS |
| `ttstr const&` | String | |
| `iTJSDispatch2*` | Object | Без AddRef |
| `tTJSVariant const&` | Любой | |

## Работа с tTJSVariant

```cpp
// Получение значений
tTVInteger i = variant->AsInteger();   // как int64
ttstr s = *variant;                     // как строка
tTVReal r = variant->AsReal();         // как double
iTJSDispatch2* obj = variant->AsObjectNoAddRef();  // как объект (без AddRef)

// Проверка типа
tTJSVariantType type = variant->Type();
if (type == tvtString) { /* строка */ }

// Установка
*result = (tTVInteger)42;
*result = TJS_W("hello");
```

## TVPExecuteExpression — выполнение TJS из C++

```cpp
ttstr code = TJS_W("kag.historyLayer.currentName");
tTJSVariant result;
TVPExecuteExpression(code, &result);
ttstr name = result;  // → "Natsuki"
```

## Работа с iTJSDispatch2 (объекты)

```cpp
// Получить свойство
tTJSVariant val;
obj->PropGet(0, TJS_W("propertyName"), nullptr, &val, nullptr);

// Установить свойство
tTJSVariant val(TJS_W("value"));
obj->PropSet(TJS_MEMBERENSURE, TJS_W("propertyName"), nullptr, &val, obj);

// Вызвать метод
tTJSVariant result;
tTJSVariant* params[] = { &arg1, &arg2 };
obj->FuncCall(0, TJS_W("methodName"), nullptr, &result, 2, params, nullptr);

// Перечислить члены (ключи объекта)
// Использовать EnumMembers с callback-классом
```
