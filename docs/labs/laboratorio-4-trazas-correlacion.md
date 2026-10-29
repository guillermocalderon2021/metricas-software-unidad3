# Laboratorio 4. Trazas distribuidas y correlación de señales

**Curso:** Métricas de Software  
**Modalidad:** parejas
**Valor:** 6%    
**Fecha limite de entrega:** Jueves 5 de noviembre
**Punto de partida:** repositorio propio entregado en el Laboratorio 3  
**Entrega:** URL del repositorio privado y SHA completo del commit evaluable  
**Documento de respuestas:** `LAB4.md` en el mismo repositorio


## Propósito

En el Laboratorio 3, cada pareja instrumentó los servicios `notifications-api` y `delivery-provider` mediante logs estructurados, métricas Prometheus, consultas PromQL y un dashboard de Grafana. El Laboratorio 4 continúa **desde esa implementación propia**: incorpora trazas distribuidas con OpenTelemetry y utiliza las tres señales para investigar una degradación de duración.

**Pregunta central:** ¿cómo reconstruir una ejecución distribuida y correlacionar métricas, trazas y logs para localizar un comportamiento anómalo y formular conclusiones sustentadas en evidencia?

Al finalizar, la pareja deberá poder:

1. instrumentar dos procesos Node.js y obtener en Jaeger una traza que atraviese ambos servicios;
2. comprobar la propagación del contexto y las relaciones entre spans;
3. diseñar e implementar al menos un span manual de dominio en cada servicio;
4. representar escenarios, resultados, eventos y estados sin confundir un error intermedio con el resultado global;
5. asociar los registros Pino con `trace_id` y `span_id`, conservando `request_id`;
6. analizar los cuatro escenarios y desarrollar una investigación profunda de `slow` mediante métricas, trazas y logs;
7. diferenciar observaciones, interpretaciones, hipótesis y conclusiones.

**No se solicita implementar un script de validación nuevo.** Las comprobaciones requeridas se demostrarán mediante consultas, trazas, comandos y análisis documentados en `LAB4.md`.


### Sistema y escenarios

```text
cliente
   |
   | POST /notifications
   v
notifications-api
   |
   | POST /deliver
   v
delivery-provider
```

| Escenario | Comportamiento programado | Aspecto principal que debe analizarse |
|---|---|---|
| `normal` | Respuesta del proveedor tras aproximadamente 80 ms | Recorrido y relación entre spans |
| `slow` | Respuesta del proveedor tras aproximadamente 350 ms | Localización de la diferencia temporal |
| `timeout` | Respuesta 504 tras aproximadamente 700 ms | Resultados, eventos y estado de spans |
| `retry-success` | Primer intento 503, segundo intento 202 | Intentos, recuperación y resultado global |

Estos tiempos provienen de las demoras programadas del entorno de laboratorio. **No son predicciones exactas** de los tiempos que aparecerán en Jaeger: los spans pueden abarcar trabajo adicional, espera, instrumentación y comunicación.

Los identificadores cumplen funciones distintas:

| Identificador | Propósito |
|---|---|
| `notification_id` | Identificar una notificación del dominio |
| `request_id` | Correlacionar registros de la aplicación mediante `X-Request-ID` |
| `trace_id` | Identificar una ejecución distribuida instrumentada |
| `span_id` | Identificar una operación concreta dentro de una traza |

**Restricción de cardinalidad:** no agregar `request_id`, `notification_id`, `trace_id` ni `span_id` como labels de Prometheus.  
**Restricción de privacidad:** no persistir `recipient`, `message` ni cuerpos completos de solicitudes, tanto en logs como en nombres, atributos o eventos de spans.  
**Restricción funcional:** preservar los contratos HTTP, respuestas, reintentos, logs JSON Lines, métricas y dashboard del Laboratorio 3.

En el Laboratorio 4 se permiten cambios **controlados** en `compose.yaml`, Dockerfiles, archivos de inicialización, `logging.ts` y otros archivos cuando sean necesarios para la trazabilidad. Esto amplía las restricciones de modificación establecidas para el Laboratorio 3; no autoriza reemplazar indiscriminadamente el trabajo anterior.

---

# Parte I. Recuperar el trabajo del Laboratorio 3

## 1. Establecer una línea base identificable

Identifique el SHA completo del commit entregado en el Laboratorio 3. Antes de crear una rama, asegúrese de que no hay cambios locales sin guardar:

```bash
git status --short
git rev-parse HEAD
git log -1 --oneline
```

Si existen cambios sin confirmar, resguárdelos antes de continuar. Para crear la rama desde el commit **efectivamente entregado**, sustituya el marcador siguiente por su SHA real:

```bash
SHA_LAB3='REEMPLAZAR_CON_SHA_REAL'
git switch -c lab4-tracing "$SHA_LAB3"
git rev-parse HEAD
```

Antes de incorporar OpenTelemetry, compruebe que el sistema desarrollado en el Laboratorio 3 continúa funcionando.

No es necesario reinstalar dependencias ni reconstruir las imágenes Docker si el entorno existente está operativo y corresponde a la versión del código que se desea comprobar.

### Paso 1. Comprobar el entorno

Ejecute:

```bash
npm run build
docker compose ps
```

Si la compilación falla porque faltan las dependencias locales, ejecute:

```bash
npm ci
npm run build
```

Si los contenedores no están ejecutándose, inícielos:

```bash
docker compose up -d
```

Si las imágenes existentes no corresponden al código actual, reconstruya los servicios:

```bash
docker compose up --build -d
```

### Paso 2. Comprobar el comportamiento existente

Con ambos servicios en ejecución, ejecute:

```bash
npm run probe:privacy
npm run probe:retry

curl -fsS http://localhost:3000/health
curl -fsS http://localhost:3002/health

curl -fsS http://localhost:3000/metrics \
  > /tmp/lab4-notifications-metrics.txt

curl -fsS http://localhost:3002/metrics \
  > /tmp/lab4-delivery-metrics.txt
```

Compruebe que los logs estructurados siguen generándose y conservan `request_id`.

Las pruebas de privacidad y reintentos comprueban las respuestas HTTP esperadas. También debe revisarse que los logs no contengan datos sensibles y que los eventos permitan reconstruir los reintentos.


Verifique además que los dos targets de Prometheus están disponibles en `http://localhost:9090/targets` y que su dashboard previo sigue mostrando métricas. 

**En `LAB4.md`, sección 1:** registre el SHA de partida, el estado básico de las verificaciones y cualquier reparación indispensable para continuar. Si una comprobación falla, documente qué se observó y qué se modificó antes de avanzar.

---

# Parte II. Incorporar la infraestructura y obtener trazas automáticas

## 2. Reconocer la estructura de arranque y el cliente HTTP

Inspeccione antes de editar:

```text
package.json
package-lock.json
compose.yaml
services/notifications-api/Dockerfile
services/delivery-provider/Dockerfile
services/notifications-api/src/server.ts
services/delivery-provider/src/server.ts
services/notifications-api/src/logging.ts
services/delivery-provider/src/logging.ts
services/notifications-api/src/delivery-client.ts
```

Los servicios están escritos en TypeScript, compilan a JavaScript ESM (`NodeNext`) y arrancan con `node dist/server.js`. El cliente de `notifications-api` utiliza `fetch()` nativo de Node.js, implementado sobre Undici. Por ello, deben observarse tanto los spans HTTP de Express como los spans de la llamada HTTP saliente.

OpenTelemetry necesita registrar la instrumentación **antes de importar los módulos que se desean instrumentar**. Los cambios de arranque se realizarán sin reescribir los endpoints.

## 3. Incorporar Jaeger y OpenTelemetry Collector

En esta sección se proporciona la configuración completa de infraestructura. Su incorporación no constituye la parte central evaluada del laboratorio.

### 3.1 Archivo completo `otel-collector-config.yaml`

Cree en la raíz del repositorio el archivo siguiente:

```yaml
receivers:
  otlp:
    protocols:
      grpc:
        endpoint: 0.0.0.0:4317
      http:
        endpoint: 0.0.0.0:4318

processors:
  batch: {}

exporters:
  debug:
    verbosity: basic
  otlp_grpc/jaeger:
    endpoint: jaeger:4317
    tls:
      insecure: true

service:
  pipelines:
    traces:
      receivers: [otlp]
      processors: [batch]
      exporters: [debug, otlp_grpc/jaeger]
```

En este pipeline, el Collector recibe OTLP, agrupa spans y los exporta hacia Jaeger. El exporter `debug` también ofrece evidencia operativa en los logs del Collector. **No** se enviarán métricas ni logs mediante este pipeline: las métricas seguirán en Prometheus y los registros seguirán en archivos JSON Lines.

### 3.2 Servicios completos para incorporar en `compose.yaml`

**Agregue** las siguientes entradas dentro de la sección `services:` existente, conservando las entradas de `notifications-api`, `delivery-provider`, `prometheus` y `grafana`:

```yaml
  jaeger:
    image: cr.jaegertracing.io/jaegertracing/jaeger:2.22.0
    ports:
      - "16686:16686"

  otel-collector:
    image: otel/opentelemetry-collector:0.163.0
    command:
      - "--config=/etc/otelcol/config.yaml"
    volumes:
      - ./otel-collector-config.yaml:/etc/otelcol/config.yaml:ro
    depends_on:
      - jaeger
```

No se necesita publicar los puertos OTLP 4317 y 4318 en el anfitrión, porque los contenedores se comunican por la red de Compose. Jaeger usa en este ejercicio almacenamiento temporal en memoria; **reiniciar o recrear el contenedor puede hacer perder trazas**. Conserve las capturas antes de limpiar el entorno.

### 3.3 Variables de entorno completas para cada servicio

Añada estas tres entradas al `environment:` existente de `notifications-api`:

```yaml
      OTEL_SERVICE_NAME: "notifications-api"
      OTEL_EXPORTER_OTLP_TRACES_ENDPOINT: "http://otel-collector:4318/v1/traces"
      OTEL_TRACES_SAMPLER: "always_on"
```

Añada las correspondientes a `delivery-provider`:

```yaml
      OTEL_SERVICE_NAME: "delivery-provider"
      OTEL_EXPORTER_OTLP_TRACES_ENDPOINT: "http://otel-collector:4318/v1/traces"
      OTEL_TRACES_SAMPLER: "always_on"
```

El muestreo `always_on` se utiliza únicamente para facilitar la observación de las solicitudes de esta actividad controlada. No representa por sí mismo una política apropiada para sistemas productivos.

Conserve las variables previas como `PORT`, `LOG_FILE` y `DELIVERY_PROVIDER_URL`. Puede añadir una dependencia de arranque hacia `otel-collector`, pero debe preservar las dependencias actuales entre servicios. Una dependencia de Compose no garantiza, por sí sola, que el SDK y el backend ya estén listos para recibir datos.

### 3.4 Comprobación de infraestructura

```bash
docker compose config
docker compose up -d jaeger otel-collector
docker compose ps
docker compose logs --tail=60 jaeger otel-collector
```

Abra `http://localhost:16686`. La interfaz de Jaeger puede estar disponible sin mostrar servicios hasta que las aplicaciones comiencen a exportar trazas. La disponibilidad de la interfaz **no demuestra** propagación entre procesos.

## 4. Agregar dependencias reproducibles

Las siguientes versiones se seleccionan como un conjunto fijo de la familia OpenTelemetry vigente al preparar esta versión del laboratorio. La instalación debe conservarse en el lockfile para evitar cambios posteriores de resolución:

```bash
npm install --save-exact \
  @opentelemetry/api@1.10.0 \
  @opentelemetry/sdk-node@0.300.0 \
  @opentelemetry/auto-instrumentations-node@0.82.0 \
  @opentelemetry/instrumentation@0.300.0 \
  @opentelemetry/exporter-trace-otlp-http@0.300.0
```

Estos paquetes son dependencias de **ejecución**. No deben instalarse únicamente como `devDependencies`: los contenedores finales instalan solo dependencias de producción. Se utiliza el paquete `auto-instrumentations-node`, que incluye la instrumentación de Pino y Undici; no deben registrarse duplicados de estas instrumentaciones.

```bash
npm ls @opentelemetry/api @opentelemetry/sdk-node \
  @opentelemetry/auto-instrumentations-node \
  @opentelemetry/instrumentation \
  @opentelemetry/exporter-trace-otlp-http
npm run build
```

Compruebe que `package.json` y `package-lock.json` contienen los cambios y serán incluidos en el commit. No sustituya las dependencias del Laboratorio 3 por un `package.json` nuevo.

**Nota técnica de esta edición:** las versiones se han cotejado con los paquetes publicados; la combinación exacta con los Dockerfiles, Pino 10 y Node 22 permanece pendiente de prueba integral docente.

## 5. Preparar el arranque de OpenTelemetry

### 5.1 Implementar el bootstrap

Cree en la raíz del repositorio:

```text
instrumentation.cjs
```

Se utiliza CommonJS para la inicialización y se mantiene ESM para los servicios compilados. Ambos procesos compartirán el archivo de bootstrap; `OTEL_SERVICE_NAME` los identificará por separado.

El bootstrap debe cumplir estos requisitos:

1. crear e iniciar un `NodeSDK` **antes** de cargar Express, Pino y el cliente HTTP;
2. configurar `OTLPTraceExporter` por HTTP usando `OTEL_EXPORTER_OTLP_TRACES_ENDPOINT`;
3. registrar las autoinstrumentaciones que permitan observar Express, HTTP y `fetch()`/Undici;
4. conservar la propagación W3C `traceparent` entre `notifications-api` y `delivery-provider`;
5. evitar exportar nuevamente las métricas de Prometheus por OTLP;
6. controlar el cierre del SDK ante la terminación del proceso, sin alterar la respuesta HTTP.

Punto de partida **parcial**; debe completarse la inicialización:

```js
const { NodeSDK } = require('@opentelemetry/sdk-node');
const { getNodeAutoInstrumentations } =
  require('@opentelemetry/auto-instrumentations-node');
const { OTLPTraceExporter } =
  require('@opentelemetry/exporter-trace-otlp-http');

// Configure el exporter y las autoinstrumentaciones.
// Cree e inicie el NodeSDK antes de cargar la aplicación.
// Revise la gestión de señales de terminación.
```

**Configuración específica de Pino:** si utiliza `getNodeAutoInstrumentations`, configure `@opentelemetry/instrumentation-pino` con `disableLogSending: true`. El laboratorio utilizará su correlación con el contexto de tracing, pero no habilitará el envío OTLP de logs. El método preciso de configuración y su comprobación forman parte de la implementación guiada.

La instrumentación de Pino puede agregar `trace_id`, `span_id` y `trace_flags` al registro cuando existe un contexto activo válido. No se deben duplicar manualmente esos campos si ya se inyectan correctamente.

### 5.2 Actualizar ambos Dockerfiles

Los Dockerfiles del repositorio base instalan paquetes sin utilizar el lockfile y solo copian `dist/` al contenedor final. Actualice **ambas etapas** de cada Dockerfile respetando los nombres y rutas propios de cada servicio.

En la etapa de construcción, sustituya la copia e instalación de dependencias por:

```dockerfile
COPY package.json package-lock.json tsconfig.base.json ./
RUN npm ci
```

En la etapa final, sustituya la copia e instalación de dependencias por:

```dockerfile
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
```

Conserve la compilación TypeScript y la copia del directorio `dist` correspondiente al servicio. Agregue también el bootstrap a la etapa final:

```dockerfile
COPY instrumentation.cjs ./instrumentation.cjs
```

El comando de arranque se proporciona completo porque memorizar flags de precarga no constituye un objetivo evaluado:

```dockerfile
CMD ["node", "--experimental-loader=@opentelemetry/instrumentation/hook.mjs", "--require", "./instrumentation.cjs", "dist/server.js"]
```

El hook se utiliza para instrumentar módulos ESM. El bootstrap debe iniciarse antes del código de la aplicación. Si decide usar un archivo de bootstrap distinto, preserve la misma relación de precarga.

### 5.3 Reconstruir y comprobar la producción automática de spans

```bash
npm run build
docker compose up --build -d
docker compose ps
docker compose logs --tail=70 notifications-api delivery-provider otel-collector
```

Los spans pueden tardar brevemente en aparecer porque el SDK y el Collector procesan y exportan en lotes.

Use este comando reutilizable desde **Bash**. Cambie únicamente `MODO` por `normal`, `slow`, `timeout` o `retry-success`:

```bash
MODO=normal
ID="$(date +%s)-${RANDOM}"
curl -sS -w '\nHTTP %{http_code}\n' \
  -X POST http://localhost:3000/notifications \
  -H 'content-type: application/json' \
  -H "x-request-id: req-lab4-${MODO}-${ID}" \
  -H "x-lab-scenario: ${MODO}" \
  -d "{\"notification_id\":\"not-lab4-${MODO}-${ID}\",\"channel\":\"email\",\"template\":\"account-activity\",\"recipient\":\"student@example.test\",\"message\":\"Mensaje sintetico de prueba\"}"
```

El comando crea identificadores diferentes para facilitar la búsqueda. Estos valores son de prueba; nunca deben incorporarse directamente a una explicación como si fueran el resultado observado de Jaeger.

En `http://localhost:16686`:

1. busque la ejecución reciente de `notifications-api`;
2. identifique el span servidor `POST /notifications`;
3. ubique la llamada cliente saliente a `/deliver` y la recepción servidor en `delivery-provider`;
4. compruebe que los spans pertenecen al **mismo `trace_id`**;
5. compruebe además que el span servidor remoto tiene la relación de contexto esperada respecto del span cliente.

La autoinstrumentación puede generar spans adicionales o utilizar convenciones de nombres distintas según la versión. Busque por `service.name`, atributos HTTP, clase de span y relaciones, no por un número fijo de spans ni por cadenas exactas de nombre.

**En `LAB4.md`, sección 2:** describa el comportamiento observado y registre el criterio que demuestra la propagación real. No basta con mostrar dos spans que tengan horas similares.

### Diagnóstico de fallos, antes de avanzar

| Observación | Primer elemento que debe investigarse |
|---|---|
| No aparecen spans de ningún servicio | Precarga, bootstrap, errores del SDK, endpoint OTLP |
| Aparecen spans de `notifications-api`, pero no de `delivery-provider` | Inicialización del segundo proceso y exportación |
| Aparecen ambos servicios, pero en trazas independientes | Instrumentación de `fetch()`/Undici y propagación de `traceparent` |
| Collector recibe spans, pero Jaeger no los muestra | Exporter `otlp_grpc/jaeger`, conectividad y logs de Jaeger |
| Los spans aparecen, pero no hay campos de traza en Pino | Contexto activo del log y configuración de instrumentación de Pino |

No intente resolver un problema de propagación modificando arbitrariamente `request_id` o las respuestas funcionales. Registre la observación que motiva cada corrección.

---

# Parte III. Instrumentación manual de operaciones

## 6. Diseñar los spans antes de programarlos

Los spans HTTP automáticos describen límites de comunicación; no siempre describen operaciones significativas para el dominio.

Examine `notifications.ts` y `delivery.ts` de su propia solución del Laboratorio 3. Complete en `LAB4.md` una tabla con **al menos una operación de dominio por servicio**:

| Servicio | Pregunta que responderá el span | Nombre propuesto | Inicio y fin reales | Atributos | Resultados |
|---|---|---|---|---|---|
| `notifications-api` | | | | | |
| `delivery-provider` | | | | | |

Justifique los límites temporales. Por ejemplo, un span cuyo nombre promete describir el procesamiento de una notificación no puede terminar antes de conocer el resultado de la entrega. Si decide excluir la validación inicial del intervalo, explíquelo.

El diseño debe distinguir las responsabilidades de ambos servicios, evitando representar como una única operación todos los detalles generados automáticamente por HTTP.

## 7. Implementar spans, atributos, eventos y estados

El siguiente fragmento orienta la elección de APIs, pero **no proporciona la instrumentación de dominio resuelta**:

```ts
import { trace, SpanStatusCode } from '@opentelemetry/api';

const tracer = trace.getTracer('instrumentacion-dominio');

// Determine el lugar de inicio y fin de cada span.
// Use el contexto activo para conservar las relaciones padre-hijo.
// Agregue atributos y eventos pertinentes y cierre cada span.
```

Puede utilizar `startActiveSpan`, `setAttribute`, `addEvent`, `recordException`, `setStatus` y `end` según corresponda. El diseño debe cubrir retornos anticipados, errores de operación y excepciones reales; no deje spans abiertos.

**Requisitos mínimos comprobables:**

- existe al menos un span manual de dominio en cada servicio;
- ambos están asociados correctamente con su contexto HTTP;
- contienen atributos que permiten distinguir el escenario y el resultado de la operación sin incluir datos sensibles;
- en `timeout` se registra al menos un **evento de dominio justificado**, con un nombre estable y atributos pertinentes;
- se distinguen un resultado HTTP de error, el estado del span y una excepción real;
- en `retry-success` el resultado global puede ser satisfactorio aunque el primer intento falle.

Un 503 o 504 devuelto intencionalmente por el escenario no constituye, por sí mismo, una excepción lanzada en JavaScript. No invente objetos `Error` para satisfacer una evidencia inexistente. El estado `ERROR` debe utilizarse cuando el resultado de la **operación representada por ese span** sea fallido; un intento recuperable no obliga a marcar como fallida la notificación completa.

No se exige un span manual adicional para cada intento si la autoinstrumentación HTTP proporciona los spans cliente y servidor pertinentes.

## 8. Verificar las relaciones de spans

Genere ejecuciones `normal` y `retry-success` con el comando de la sección 5.3. Compruebe las relaciones, no solo los nombres.

En `LAB4.md`, sección 3, complete con datos observados:

| Servicio | Span | Tipo (`SERVER`/`CLIENT`/`INTERNAL`) | Span padre | Qué información agrega |
|---|---|---|---|---|
| | | | | |

Explique cuál es la contribución de sus spans manuales frente a los spans automáticos. Un span manual sin pregunta operacional o que duplica exactamente a otro no constituye una mejora significativa de trazabilidad.

---

# Parte IV. Correlación de trazas y logging estructurado

## 9. Integrar el contexto de tracing con Pino

Conserve los loggers Pino y la salida a `stdout` y `logs/*.jsonl` definidos en el Laboratorio 3. Conserve también `request_id`, `notification_id` cuando corresponda y sus campos de evento estructurado.

**Vía de referencia:** utilice la instrumentación de Pino incluida en `getNodeAutoInstrumentations`, con correlación de trazas habilitada y envío de logs por OTLP deshabilitado mediante `disableLogSending: true`. Revise la configuración del bootstrap y compruebe si la inyección funciona en los registros emitidos mediante `req.log` y en el logger base utilizado por su solución.

Se permite un enriquecimiento explícito alternativo basado en `context`, `trace` e `isSpanContextValid` de `@opentelemetry/api`, siempre que produzca resultados equivalentes, preserve los destinos Pino y evite añadir identificadores inválidos o duplicados. **No implemente ambas vías simultáneamente sin justificarlo.**

El resultado esperado de un registro emitido dentro de un span activo es similar a este esquema **ilustrativo, no experimental**:

```json
{
  "event": "delivery.completed",
  "request_id": "req-de-prueba",
  "trace_id": "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  "span_id": "yyyyyyyyyyyyyyyy"
}
```

Los valores anteriores son marcadores didácticos, no identificadores válidos de una traza real. Un registro emitido fuera del contexto de una solicitud, por ejemplo `service_started`, **puede carecer correctamente** de `trace_id` y `span_id`.

Distintos spans de una misma traza comparten `trace_id` y tienen `span_id` diferentes. No exija que todos los registros de una solicitud posean el mismo `span_id`.

## 10. Comprobar una correlación real

Genere una solicitud `timeout` siguiendo la sección 5.3. Busque la ejecución en Jaeger y **copie de la interfaz el `trace_id` real**. Después, desde la raíz del repositorio, ejecute:

```bash
TRACE_ID='REEMPLAZAR_POR_TRACE_ID_REAL'
jq -c --arg id "$TRACE_ID" \
  'select(.trace_id == $id) | {time, service, event, request_id, trace_id, span_id, level}' \
  logs/notifications-api.jsonl
jq -c --arg id "$TRACE_ID" \
  'select(.trace_id == $id) | {time, service, event, request_id, trace_id, span_id, level}' \
  logs/delivery-provider.jsonl
```

La clave temporal podría llamarse `time`, `timestamp` u otro nombre si la pareja cambió su configuración de Pino. Ajuste la proyección a los campos reales, pero mantenga el filtro por `trace_id`.

**La evidencia debe permitir recuperar eventos de ambos servicios para la ejecución seleccionada**. Si no aparecen, no es suficiente explicar que «se configuró OpenTelemetry»: se debe localizar el problema de contexto o de logging.

En `LAB4.md`, sección 4, incluya las consultas, una muestra breve de resultados reales y una explicación de qué papel desempeña cada identificador. No incluya archivos JSON Lines completos ni contenido sensible.

---

# Parte V. Cuatro escenarios, cuatro aspectos de interpretación

## 11. `normal`: reconstruir una solicitud

Produzca una ejecución normal. Identifique el span raíz, los spans HTTP que cruzan los dos servicios y los spans manuales. Explique en un párrafo cómo la estructura permite reconstruir la operación y qué evidencia confirma la propagación de contexto.

No se requiere una captura adicional si la evidencia queda suficientemente documentada mediante la tabla de la sección 3.

## 12. `slow`: observación preliminar

Produzca una ejecución lenta y compruebe que conserva una estructura comparable a la de `normal`. Identifique el servicio u operación donde visualmente parece concentrarse el tiempo. **No elabore todavía la tabla de duraciones**: esa comparación se realiza una sola vez, en la investigación integrada de la Parte VI.

En `LAB4.md`, sección 5, registre una observación inicial de dos o tres oraciones, sin anticipar como demostrado el mecanismo que explica la latencia.

## 13. `timeout`: resultado y eventos

Genere un caso `timeout`. Identifique el código 504 del proveedor, el código final de `notifications-api`, el resultado de los spans manuales y el evento de dominio instrumentado. Compare la información proporcionada por atributo, evento, estado y respuesta HTTP.

Conteste: ¿la presencia de un HTTP 504 prueba que se lanzó una excepción JavaScript? ¿Permite concluir que hubo congestión de red? Fundamente la respuesta con el escenario ejecutado y la telemetría observada.

## 14. `retry-success`: recuperación después de un fallo

Genere `retry-success` o utilice `npm run probe:retry`. Compruebe **dos intentos HTTP distintos** hacia `/deliver`: el primero devuelve 503 y el segundo 202. La operación global de `notifications-api` termina 202.

Reconstruya el orden de intentos mediante spans, atributos y registros; identifique cuál componente conoce el resultado global. Explique por qué un error de intento no equivale necesariamente a un fallo final de notificación.

Conserve una captura legible:

```text
evidence/lab4-trace-retry.png
```

La captura debe permitir reconocer ambos intentos y, cuando sea posible, los servicios y sus relaciones. Si la interfaz no muestra simultáneamente todos los atributos, documente los valores faltantes mediante la tabla de `LAB4.md`.

---

# Parte VI. Investigación integrada de la fase D

## 15. Investigar una degradación desde las tres señales

La actividad principal se realizará sobre el escenario A-E del Laboratorio 3. Se utilizará **B como referencia normal y D como intervalo de degradación**, porque ambas fases programan una tasa de 4 solicitudes por segundo. Esto reduce una diferencia de carga entre las condiciones comparadas, sin controlar todos los factores experimentales.

| Fase | Tráfico programado | Escenario |
|---|---|---|
| A | 1 req/s | normal |
| **B** | **4 req/s** | **normal: referencia** |
| C | 4 req/s | mezcla de normal y timeout |
| **D** | **4 req/s** | **slow: degradación** |
| E | 1 req/s | normal |

### Paso 1. Obtener una ejecución identificable

Con los servicios instrumentados, compruebe que Prometheus, Grafana, Collector y Jaeger están disponibles. Registre la hora de inicio del experimento; después ejecute:

```bash
date -Is
npm run scenario
date -Is
```

Ajuste el intervalo del dashboard para cubrir el experimento completo. Si se ejecuta el escenario varias veces, recuerde que el script reutiliza algunos `request_id` de fases anteriores, mientras que cada nueva ejecución instrumentada recibe nuevos `trace_id`.

### Paso 2. Caracterizar el síntoma con una métrica

Recupere las consultas y el dashboard del Laboratorio 3. Seleccione una expresión PromQL que represente duración de `POST /notifications` o de la dependencia, e indique:

- expresión exacta, unidad y ventana temporal;
- qué magnitud representa y qué puede inferirse sobre B y D;
- cambios observados, con cifras extraídas del dashboard;
- limitaciones de la resolución temporal o de buckets del histograma.

Si utiliza p95 a partir de `histogram_quantile`, indique que se trata de una **estimación dependiente de los buckets** diseñados en el Laboratorio 3. No invente precisión adicional ni modifique las consultas solo para producir la figura esperada.

### Paso 3. Seleccionar una traza de cada condición

Busque en Jaeger una ejecución `normal` **de B** y una ejecución `slow` **de D**, apoyándose en el intervalo, el servicio y los atributos de escenario disponibles. Registre ambos `trace_id` y explique cómo confirmó su pertenencia a cada condición.

Una métrica agregada no identifica automáticamente la traza de la muestra que determina su p95. La selección de trazas permite examinar ejecuciones concretas del mismo contexto, no demuestra por sí sola una correspondencia exacta entre una observación agregada y una solicitud individual.

### Paso 4. Comparar duraciones sin duplicar intervalos

Complete **una sola tabla** con los valores observados en Jaeger:

| Operación comparable | B, normal (ms) | D, slow (ms) | Diferencia (ms) |
|---|---:|---:|---:|
| Span HTTP de entrada a `notifications-api` | | | |
| Span manual de `notifications-api` | | | |
| Span HTTP cliente de `/deliver` | | | |
| Span HTTP servidor de `delivery-provider` | | | |
| Span manual de `delivery-provider` | | | |

Si alguna instrumentación produce una estructura distinta, explique la correspondencia utilizada y no fuerce valores inexistentes.

Responda dónde se concentra la diferencia y por qué **no deben sumarse las duraciones de spans anidados** como si fueran intervalos independientes. Distinga tiempo transcurrido de tiempo de CPU; los spans por sí solos no permiten inferir uso continuo de CPU, congestión de red o saturación de recursos.

Conserve:

```text
evidence/lab4-trace-slow.png
```

La captura debe mostrar una ejecución lenta de D, con operaciones y tiempos legibles. La comparación con B debe poder reconstruirse mediante la tabla y los identificadores registrados en el informe.

### Paso 5. Correlacionar la traza lenta con logs de ambos servicios

Aplique las consultas de la sección 10 usando el `trace_id` seleccionado de D. Elija eventos que describan el inicio y el resultado de operaciones relevantes. Explique qué información aparece en los logs y no en los spans, y qué estructura temporal aporta la traza que no se reconstruye con la misma precisión desde los logs.

### Paso 6. Formular una hipótesis contrastable

Organice la sección 6 de `LAB4.md` con esta estructura:

| Elemento | Contenido requerido |
|---|---|
| Pregunta operacional | Qué comportamiento se busca explicar |
| Síntoma agregado | Consulta, intervalo, magnitud y unidad |
| Ejecuciones seleccionadas | `trace_id` de B y D, criterio de selección |
| Diferencia de duración | Operaciones y duraciones observadas |
| Evidencia de logs | Consultas y eventos de ambos servicios |
| Interpretación | Qué significado compatible con la evidencia puede sostenerse |
| Hipótesis | Posible mecanismo todavía no demostrado |
| Predicción contrastable | Qué observación debería obtenerse si la hipótesis fuese correcta |
| Evidencia adicional | Medición o experimento concreto para ponerla a prueba |
| Conclusión delimitada | Qué se sabe y qué continúa sin demostrarse |

Evite atribuir una causa a la red, al sistema operativo o a la CPU sin evidencia específica. El código del escenario permite conocer que existe una demora programada; explique qué parte de esa conclusión se apoya en el diseño didáctico y qué parte fue identificada mediante observabilidad.

Conserve una captura del dashboard donde el intervalo del experimento sea legible:

```text
evidence/lab4-investigation.png
```

---

# Parte VII. Comprobación final y entrega

## 16. Revisar comportamiento, correlación y privacidad

No se requiere desarrollar ni ejecutar un validador nuevo. Antes de entregar, realice estas comprobaciones sobre los contenedores actualizados:

```bash
npm run build
docker compose up --build -d
npm run probe:retry
npm run probe:privacy
curl -fsS http://localhost:3000/metrics > /tmp/lab4-final-notifications-metrics.txt
curl -fsS http://localhost:3002/metrics > /tmp/lab4-final-delivery-metrics.txt
```

Verifique manualmente que:

1. los cuatro escenarios mantienen los resultados funcionales previstos;
2. se conservan los eventos JSON Lines, `request_id` y las métricas del Laboratorio 3;
3. ambos servicios producen spans HTTP y spans manuales correctamente relacionados;
4. los logs de una ejecución seleccionada se pueden recuperar mediante `trace_id`;
5. los valores sensibles utilizados por `probe:privacy` no aparecen en logs ni en los atributos o eventos de trazas.

Para comprobar los valores sensibles **en los archivos de logs existentes**, ejecute:

```bash
grep -nE 'DO-NOT-LOG@example\.test|SECRET-LAB3-DO-NOT-PERSIST-9381' \
  logs/notifications-api.jsonl logs/delivery-provider.jsonl
```

La ausencia de coincidencias es el resultado esperado en los archivos. **Este comando no verifica Jaeger**: la privacidad de atributos y eventos exportados se revisará también inspeccionando en la traza correspondiente la información efectivamente persistida. No copie contenido sensible al informe.

**En `LAB4.md`, sección 7:** documente el resultado de estas comprobaciones y las limitaciones que subsistan. No se solicita reproducir toda la salida de los comandos.

## 17. Preparar evidencias y publicar el commit

Cree `LAB4.md` usando la **plantilla del Anexo A** de este mismo documento. Debe conservar también los archivos y evidencias del Laboratorio 3.

La nueva evidencia mínima será:

```text
LAB4.md
evidence/
├── lab4-trace-slow.png
├── lab4-trace-retry.png
└── lab4-investigation.png
```

Las capturas deben ser legibles: interfaz, servicio, intervalo y operaciones pertinentes. Los `trace_id` exactos, las consultas y las interpretaciones se registrarán en `LAB4.md`.

No agregue al repositorio `node_modules`, `dist`, archivos JSON Lines generados, almacenamiento interno de Jaeger, Prometheus o Grafana, ni datos sensibles. Conserve `package-lock.json`, `instrumentation.cjs`, `otel-collector-config.yaml`, Dockerfiles modificados y el código fuente instrumentado.

Revise primero qué archivos serán incluidos:

```bash
git status --short
git diff --stat
git add package.json package-lock.json compose.yaml \
  instrumentation.cjs otel-collector-config.yaml \
  services/notifications-api services/delivery-provider \
  LAB4.md evidence/
git diff --cached --stat
git commit -m "Complete Lab 4 distributed tracing"
git push -u origin lab4-tracing
git rev-parse HEAD
```

Si modificó algún archivo adicional necesario, inclúyalo deliberadamente después de revisarlo. Compruebe que el SHA comunicado en Moodle corresponde a la rama publicada y que el docente mantiene acceso al repositorio privado.

**Entrega en Moodle:** URL del repositorio y SHA completo del commit. No se requiere PDF ni archivo independiente de validación.

---

# Rúbrica de evaluación

| Criterio | Peso | Evidencia principal |
|---|---:|---|
| Diseño de trazabilidad | 10% | Tabla de operaciones y justificación de sus intervalos |
| SDK, autoinstrumentación y propagación | 20% | Spans HTTP entre servicios y relaciones padre-hijo |
| Instrumentación manual y semántica | 25% | Spans de dominio, atributos, evento y estados coherentes |
| Correlación entre logs y trazas | 15% | Registros Pino consultados mediante `trace_id` |
| Investigación integrada | 25% | Comparación B/D, logs correlacionados e hipótesis contrastable |
| Conservación funcional y comprobaciones | 5% | Resultados de regresión y privacidad |
| **Total** | **100%** | |

## Criterios de logro

**Diseño de trazabilidad (10%).** Un diseño satisfactorio delimita operaciones de dominio y sus intervalos, identifica preguntas operacionales concretas y selecciona atributos sin redundancia ni datos sensibles. La cantidad de spans no constituye, por sí sola, un mérito.

**SDK, autoinstrumentación y propagación (20%).** Se requiere evidencia de exportación, identidad de ambos servicios y relación de contexto en una ejecución distribuida. Dos spans con el mismo nombre o producidos en el mismo intervalo no demuestran propagación.

**Instrumentación manual y semántica (25%).** Se valoran los spans correctamente anidados y cerrados, la interpretación de resultados, el uso justificado de eventos y la diferencia entre fallo de intento y fallo global. Un span que permanece abierto, representa un intervalo incoherente o contradice el resultado de la operación constituye un defecto sustancial.

**Correlación (15%).** Se requiere recuperar logs de ambos servicios desde el identificador de una traza real, sin duplicar campos ni perder `request_id`; la privacidad debe conservarse.

**Investigación integrada (25%).** Se valorará la selección pertinente de B y D, la medición e interpretación de diferencias, la explicación de los límites del p95, la correlación temporal entre señales y una hipótesis con predicción comprobable. Repetir la descripción conocida del escenario sin analizar la evidencia es insuficiente.

**Conservación y comprobaciones (5%).** La nueva instrumentación debe preservar las funcionalidades y la telemetría existentes. Las correcciones de un defecto previo del Laboratorio 3 deben justificarse, pero no se vuelve a calificar íntegramente ese laboratorio.

---

# Anexo A. Plantilla copiable de `LAB4.md`

El siguiente bloque establece la **estructura de respuestas**. Cópielo en un archivo `LAB4.md` de la raíz del repositorio y sustituya las celdas vacías por evidencia propia. No copie los encabezados como si fueran respuestas suficientes.

~~~markdown
# Laboratorio 4. Trazas distribuidas y correlación de señales

**Integrantes:**  
**Repositorio:**  
**Commit de Lab 3:**  
**Commit de Lab 4:**  

## 1. Línea base del Laboratorio 3

| Comprobación | Resultado observado | Corrección necesaria, si aplica |
|---|---|---|
| Compilación | | |
| Servicios HTTP | | |
| Logs y `request_id` | | |
| Métricas/targets | | |
| Dashboard anterior | | |

## 2. Infraestructura, autoinstrumentación y propagación

**Archivos modificados y propósito:**

**Traza elegida (`trace_id` real):**

**Spans de ambos servicios y relación padre-hijo observada:**

**Evidencia que demuestra la propagación:**

## 3. Diseño e instrumentación de spans manuales

| Servicio | Pregunta operacional | Span | Inicio/fin | Atributos | Resultados |
|---|---|---|---|---|---|
| `notifications-api` | | | | | |
| `delivery-provider` | | | | | |

| Servicio | Span | Tipo | Span padre | Evidencia adicional |
|---|---|---|---|---|
| | | | | |

**Evento de dominio elegido y justificación:**

**Tratamiento de errores, excepciones y cierre de spans:**

## 4. Correlación con logs Pino

**Implementación adoptada:**

**`trace_id` utilizado:**

**Consultas `jq` para ambos servicios:**

**Selección breve de eventos encontrados:**

**Diferencia entre `notification_id`, `request_id`, `trace_id` y `span_id`:**

## 5. Análisis breve de los cuatro escenarios

### `normal`

**Recorrido observado y evidencia de contexto:**

### `slow`

**Observación preliminar; la tabla de duraciones se presenta en la sección 6:**

### `timeout`

**Código HTTP, atributo, evento y estado observados:**

**Diferencia entre resultado HTTP y excepción JavaScript:**

### `retry-success`

**Primer intento, segundo intento y resultado global:**

**Por qué un intento fallido puede coexistir con una notificación exitosa:**

**Captura:** `evidence/lab4-trace-retry.png`

## 6. Investigación integrada: B frente a D

**Consulta PromQL, magnitud, unidad y ventana:**

**Síntoma en B y D, con valores observados:**

**Limitaciones de la métrica y de sus buckets:**

**Traza normal de B (`trace_id`):**

**Traza lenta de D (`trace_id`):**

**Criterio de selección de las trazas:**

| Operación comparable | B, normal (ms) | D, slow (ms) | Diferencia (ms) |
|---|---:|---:|---:|
| Entrada HTTP a `notifications-api` | | | |
| Span manual de `notifications-api` | | | |
| CLIENT `/deliver` | | | |
| SERVER `delivery-provider` | | | |
| Span manual de `delivery-provider` | | | |

**Por qué no se suman duraciones de spans anidados:**

**Consultas y eventos de logs relacionados con D:**

| Elemento | Análisis |
|---|---|
| Pregunta operacional | |
| Evidencia agregada | |
| Evidencia de trazas | |
| Evidencia de logs | |
| Interpretación | |
| Hipótesis | |
| Predicción contrastable | |
| Evidencia adicional requerida | |
| Conclusión delimitada | |

**Capturas:** `evidence/lab4-trace-slow.png` y `evidence/lab4-investigation.png`

## 7. Comprobaciones y límites

| Comprobación | Resultado |
|---|---|
| Compilación | |
| Comportamiento de los escenarios | |
| Métricas anteriores | |
| Propagación y spans de dominio | |
| Logs correlacionados | |
| Privacidad en JSONL y trazas | |

**Limitaciones que permanecen y evidencia adicional que sería necesaria:**
~~~

---

# Referencias técnicas

1. OpenTelemetry, [Getting Started with Node.js](https://opentelemetry.io/docs/languages/js/getting-started/nodejs/).
2. OpenTelemetry JavaScript, [Instrumentación de aplicaciones ESM](https://github.com/open-telemetry/opentelemetry-js/blob/main/doc/esm-support.md).
3. OpenTelemetry, [Traces](https://opentelemetry.io/docs/concepts/signals/traces/) y [Context propagation](https://opentelemetry.io/docs/concepts/context-propagation/).
4. OpenTelemetry, [Collector configuration](https://opentelemetry.io/docs/collector/configuration/).
5. OpenTelemetry, [Instrumentación de Pino](https://github.com/open-telemetry/opentelemetry-js-contrib/tree/main/packages/instrumentation-pino).
6. OpenTelemetry, [Instrumentación de Undici](https://www.npmjs.com/package/@opentelemetry/instrumentation-undici).
7. Jaeger 2.22, [Getting Started](https://www.jaegertracing.io/docs/2.22/getting-started/).
8. Docker, [Dockerfile reference](https://docs.docker.com/reference/dockerfile/).
9. Repositorio base: [Métricas de Software, Laboratorio 3](https://github.com/guillermocalderon2021/metricas-software-lab3-template).
