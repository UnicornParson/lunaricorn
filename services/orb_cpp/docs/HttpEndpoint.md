# Спецификация HttpEndpoint

## 1. Назначение

`HttpEndpoint` является HTTP-интерфейсом внешнего доступа к возможностям сервиса.

`HttpEndpoint` предоставляет HTTP API для клиентов и преобразует HTTP-запросы в вызовы публичного API экземпляра `Engine`.

`HttpEndpoint` не реализует бизнес-логику сервиса.

Все операции над объектами, метаданными, blob-данными, поиском и управлением сервисом выполняются через `Engine`.

---

# 2. Архитектурная роль

Общая архитектура сервиса:

```
External Client
       |
       |
HttpEndpoint
       |
       |
    Engine
       |
       +----------------+
       |                |
MetadataController   BlobController
       |
StorageControllers
```

`HttpEndpoint` является одним из возможных протокольных входов:

```
              Engine
                |
    +-----------+------------+
    |           |            |
HttpEndpoint MCP Endpoint Admin Endpoint
```

Все endpoint'ы используют один экземпляр `Engine`.

---

# 3. Ответственность HttpEndpoint

HttpEndpoint отвечает только за:

* прием TCP соединений;
* обработку HTTP/1.1 протокола;
* поддержку Keep-Alive;
* разбор HTTP request;
* маршрутизацию запросов;
* преобразование HTTP параметров в вызовы Engine;
* преобразование результатов Engine в HTTP response;
* потоковую передачу blob-данных.

---

# 4. Запрещенная функциональность

HttpEndpoint не должен:

* обращаться напрямую к StorageController;
* работать напрямую с файловой системой;
* содержать правила хранения объектов;
* выполнять поиск объектов;
* выполнять обходы графа объектов;
* содержать бизнес-валидацию объектов;
* хранить состояние объектов между запросами.

Любое действие выполняется через Engine.

---

# 5. Технологический стек

Реализация:

* C++20
* Boost.Asio
* Boost.Beast

Используемый транспорт:

* TCP
* HTTP/1.1
* Keep-Alive

TLS может быть добавлен позже.

---

# 6. Структура реализации

Рекомендуемая структура:

```
http_endpoint.hpp
http_endpoint.cpp
```

Внешний интерфейс:

```cpp
class HttpEndpoint
{
public:

    HttpEndpoint(
        asio::io_context& io,
        Engine& engine);

    void start(
        uint16_t port);

    void stop();

private:

    Engine& engine_;
};
```

Внутренние классы могут быть скрыты внутри `.cpp`:

```
HttpEndpoint
    |
    +-- Listener
    |
    +-- Session
    |
    +-- RequestHandler
```

Внутренние детали HTTP реализации не экспортируются.

---

# 7. Жизненный цикл

## Запуск

При запуске:

1. Создается HttpEndpoint.
2. Передается ссылка на Engine.
3. Открывается TCP listener.
4. Начинается прием соединений.

---

## Обработка соединения

Последовательность:

```
TCP connection

      |

HTTP Session

      |

HTTP Request

      |

Route

      |

Engine call

      |

HTTP Response
```

---

# 8. Маршрутизация

Маршрутизация выполняется внутри HttpEndpoint.

Источник маршрутизации:

* HTTP method;
* URI path.

Пример:

```
GET /object/{id}

PUT /object/{id}/blob

GET /object/{id}/blob

POST /query

GET /status
```

Router не содержит бизнес-логики.

---

# 9. Работа с Engine

HttpEndpoint вызывает только публичный API Engine.

Пример:

```
HTTP Request

    |

HttpEndpoint

    |

Engine::getObject()

    |

Response

    |

HTTP Response
```

---

# 10. Blob API

Blob является бинарным потоком произвольного размера.

HttpEndpoint не знает содержимое blob.

Blob рассматривается как:

```
byte stream
```

---

# 11. PUT Blob

Обработка:

```
Client

 |

HTTP PUT

 |

HttpEndpoint

 |

Engine

 |

BlobController

 |

Storage
```

Требования:

* запрещается загружать весь blob в память;
* передача выполняется потоково;
* размер определяется через Content-Length;
* данные передаются блоками.

---

Пример:

```
PUT /object/123/blob

Content-Length: 104857600


<100 MB binary stream>
```

HttpEndpoint передает поток в Engine.

---

# 12. GET Blob

Обработка:

```
Storage

 |

Engine

 |

HttpEndpoint

 |

HTTP Response

 |

Client
```

Требования:

* запрещается создавать копию полного blob;
* данные передаются потоково;
* поддерживается передача больших объектов.

---

# 13. Metadata API

Metadata-запросы предполагают небольшие размеры данных.

Допускается полная загрузка тела запроса в память.

Пример:

```
GET /object/123

Response:

{
    id,
    metadata,
    links
}
```

Формат обмена:

* JSON

---

# 14. HTTP Response

Все ответы должны содержать:

* HTTP status code;
* Content-Type;
* Content-Length если размер известен.

Ошибки возвращаются в JSON формате.

Пример:

```json
{
    "error": "object_not_found",
    "message": "Object does not exist"
}
```

---

# 15. Коды HTTP

Используются стандартные коды:

## Успех

```
200 OK
201 Created
204 No Content
```

## Ошибки клиента

```
400 Bad Request
404 Not Found
409 Conflict
```

## Ошибки сервера

```
500 Internal Server Error
503 Service Unavailable
```

---

# 16. Управление соединениями

HttpEndpoint должен поддерживать:

* HTTP Keep-Alive;
* обработку нескольких запросов через одно соединение;
* корректное закрытие соединения.

---

# 17. Потоки выполнения

HttpEndpoint должен использовать модель Boost.Asio.

Требования:

* отсутствие глобального состояния;
* корректная работа при нескольких io_context workers;
* отсутствие блокирующих операций в HTTP потоке.

---

# 18. Логирование

HttpEndpoint логирует:

* открытие соединения;
* закрытие соединения;
* HTTP метод;
* URI;
* время обработки;
* размер запроса;
* размер ответа;
* HTTP статус.

Не логируются:

* содержимое blob;
* приватные данные объектов.

---

# 19. Конфигурация

HttpEndpoint должен получать через конфигурацию:

* адрес bind;
* порт;
* размер буфера передачи;
* количество worker threads;
* параметры timeout.

---

# 20. Расширяемость

Архитектура должна позволять добавление:

* MCP Endpoint;
* Admin Endpoint;
* CLI Endpoint;
* HTTP/2 Endpoint;
* HTTP/3 Endpoint.

Все новые протоколы должны использовать тот же Engine.

---

# 21. Критерии готовности первой версии

Первая версия HttpEndpoint считается готовой если:

* принимает HTTP/1.1 соединения;
* поддерживает Keep-Alive;
* выполняет маршрутизацию запросов;
* вызывает Engine;
* передает JSON ответы;
* передает большие blob через streaming;
* не хранит полный blob в памяти;
* корректно работает с Python и C++ клиентами;
* может быть запущена рядом с другими Endpoint'ами сервиса.
