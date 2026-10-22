<!--
Requiere en MkDocs Material:
- admonition
- attr_list
- md_in_html
- pymdownx.details
- pymdownx.superfences
-->

# Laboratorio 3. Instrumentación de un servicio mediante logging estructurado y métricas

**Modalidad:** parejas  
**Valor:** 6%  
**Repositorio base:** [metricas-software-lab3-template](https://github.com/guillermocalderon2021/metricas-software-lab3-template)

## Propósito

En este laboratorio se instrumentará un sistema compuesto por dos servicios y se utilizará la telemetría producida para investigar su comportamiento durante un escenario controlado.

El trabajo integra cuatro actividades que se han estudiado por separado:

1. diseño de eventos de logging estructurado;
2. instrumentación de métricas con Prometheus;
3. análisis de series temporales mediante PromQL y Grafana;
4. configuración de una regla de alerta.

El resultado esperado no es únicamente que el sistema produzca logs y métricas. La instrumentación debe permitir responder preguntas concretas sobre actividad, errores, duración, comportamiento de una dependencia y ejecución de una solicitud individual.

!!! question "Pregunta central"

    **¿Qué instrumentación necesita un servicio para detectar una degradación, caracterizarla mediante métricas y reconstruir las operaciones afectadas a partir de logs correlacionados?**

---

## 1. Sistema del laboratorio

El sistema representa el envío de notificaciones mediante un proveedor externo.

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

`notifications-api` recibe la solicitud del cliente y delega el envío en `delivery-provider`.

Una solicitud válida tiene la siguiente estructura:

```json
{
  "notification_id": "not-1042",
  "channel": "email",
  "template": "password-reset",
  "recipient": "student@example.test",
  "message": "Código temporal: 384921"
}
```

Los campos cumplen funciones diferentes:

| Campo | Significado |
|---|---|
| `notification_id` | identificador de la entidad de dominio |
| `channel` | canal de entrega, `email` o `sms` |
| `template` | tipo de notificación |
| `recipient` | destinatario |
| `message` | contenido de la notificación |

El sistema también utiliza un `request_id`. Este identificador corresponde a una solicitud técnica concreta y se propaga entre los dos servicios mediante `X-Request-ID`.

!!! note "Infraestructura ya implementada"

    La generación y propagación de `request_id` ya forma parte del repositorio. No debe reimplementarse. El trabajo consiste en utilizar ese contexto correctamente en la telemetría producida.

El escenario experimental controla el comportamiento del proveedor mediante `X-Lab-Scenario`. Este encabezado es parte de la infraestructura del laboratorio y no representa información del dominio.

Los comportamientos disponibles son:

| Escenario | Comportamiento de `delivery-provider` |
|---|---|
| `normal` | responde aproximadamente después de 80 ms |
| `slow` | responde aproximadamente después de 350 ms |
| `timeout` | responde aproximadamente después de 700 ms con HTTP 504 |
| `retry-success` | el primer intento responde 503 y el segundo finaliza correctamente |

Los comportamientos son deterministas. Esto permite ejecutar el mismo experimento varias veces y comparar evidencia obtenida por distintas parejas.

---

## 2. Preparación del repositorio

### Paso 1. Crear el repositorio de trabajo

1. Abra el [repositorio base](https://github.com/guillermocalderon2021/metricas-software-lab3-template).
2. Seleccione **Use this template**.
3. Cree un repositorio **privado** para la pareja.
4. Agregue al otro integrante de la pareja como colaborador.
5. Agregue al docente como colaborador.
6. Clone el repositorio creado.

No utilice un fork público.

### Paso 2. Instalar las dependencias

Desde la raíz del repositorio:

```bash
npm install
```

Compruebe que la versión de Node.js sea 22 o posterior:

```bash
node --version
```

### Paso 3. Compilar el proyecto base

Ejecute:

```bash
npm run build
```

Antes de modificar la instrumentación, el proyecto debe compilar correctamente.

### Paso 4. Limpiar evidencia previa

Ejecute:

```bash
npm run reset
```

Este comando reinicia los archivos de log utilizados durante el laboratorio.

### Paso 5. Levantar el entorno

Ejecute:

```bash
docker compose up --build -d
```

Compruebe el estado de los contenedores:

```bash
docker compose ps
```

Los componentes quedan disponibles en:

```text
notifications-api   http://localhost:3000
Grafana             http://localhost:3001
delivery-provider   http://localhost:3002
Prometheus          http://localhost:9090
```

Grafana utiliza:

```text
usuario:    admin
contraseña: admin
```

### Paso 6. Comprobar los servicios

Abra o consulte:

```text
http://localhost:3000/health
http://localhost:3002/health
```

Ambos servicios deben responder antes de continuar.

### Paso 7. Comprobar Prometheus

Abra:

```text
http://localhost:9090/targets
```

Verifique que los targets:

```text
notifications-api
delivery-provider
```

se encuentren en estado `UP`.

### Paso 8. Ejecutar el validador inicial

Ejecute:

```bash
npm run validate
```

El estado inicial del repositorio contiene instrumentación incompleta. Por esa razón se esperan fallos relacionados con logs y métricas.

!!! warning "Interpretación del validador"

    Un fallo inicial de `npm run validate` no significa que el entorno esté dañado. El validador identifica requisitos que todavía no se han implementado.

    Antes de continuar, sí deben funcionar la compilación, los servicios y los targets de Prometheus.

---

## 3. Reconocimiento del código

Antes de implementar cambios, identifique qué parte de la solución corresponde al laboratorio.

### Paso 1. Revise los archivos de `notifications-api`

Examine:

```text
services/notifications-api/src/notifications.ts
services/notifications-api/src/metrics.ts
services/notifications-api/src/logging.ts
```

En particular:

- `notifications.ts` contiene el flujo de procesamiento de una notificación;
- `metrics.ts` declara las familias de métricas que deben completarse;
- `logging.ts` configura Pino, `pino-http`, el archivo JSON Lines y el `request_id`.

### Paso 2. Revise los archivos de `delivery-provider`

Examine:

```text
services/delivery-provider/src/delivery.ts
services/delivery-provider/src/metrics.ts
services/delivery-provider/src/logging.ts
```

Identifique los caminos correspondientes a:

```text
success
timeout
retryable error
```

### Paso 3. Identifique la instrumentación deficiente

En los dos servicios existen registros deliberadamente insuficientes. Algunos contienen únicamente mensajes textuales y otros registran objetos completos de la solicitud.

Anote al menos tres problemas concretos que deberán corregirse.

No modifique todavía el código.

### Paso 4. Distinga archivos de trabajo e infraestructura

La implementación evaluada se concentra en:

```text
services/notifications-api/src/notifications.ts
services/notifications-api/src/metrics.ts
services/delivery-provider/src/delivery.ts
services/delivery-provider/src/metrics.ts
LAB3.md
evidence/
```

Los siguientes elementos se proporcionan como infraestructura del laboratorio y no deben modificarse para resolver la actividad:

```text
services/*/src/logging.ts
services/*/src/server.ts
services/notifications-api/src/delivery-client.ts
scripts/
compose.yaml
prometheus/
grafana/provisioning/
```

Si una modificación parece requerir alterar esa infraestructura, revise primero si la necesidad puede resolverse desde los archivos de trabajo.

---

# Parte I. Diseño de instrumentación

## 4. Definir la evidencia necesaria

Antes de agregar métricas o eventos de log, complete la sección **1. Diseño de instrumentación** de `LAB3.md`.

La tabla contiene estas preguntas:

| Pregunta operacional | Señal | Evidencia necesaria | Decisión de instrumentación |
|---|---|---|---|
| ¿Cómo cambia la actividad de `notifications-api`? | | | |
| ¿Qué proporción de solicitudes termina con error? | | | |
| ¿Cómo evoluciona la duración de `POST /notifications`? | | | |
| ¿Qué resultados produce `delivery-provider`? | | | |
| ¿Cómo evoluciona la duración de la dependencia? | | | |
| ¿Qué ocurrió durante una solicitud concreta? | | | |

La tabla no solicita cuatro formas distintas de redactar la misma idea. Cada columna representa una decisión diferente.

### 4.1 Columna `Pregunta operacional`

Las preguntas ya están definidas. No deben sustituirse por nombres de métricas ni por nombres de herramientas.

Por ejemplo, una pregunta como:

```text
¿Cómo cambia la actividad de notifications-api?
```

expresa qué se desea conocer del sistema.

No debe reemplazarse por:

```text
notifications_http_requests_total
```

porque el nombre de una métrica todavía no explica qué magnitud debe obtenerse ni cómo se interpretará.

### 4.2 Columna `Señal`

En esta columna describa **qué propiedad observable del sistema permitiría responder la pregunta**.

La respuesta debe indicar la naturaleza de la señal, no la sintaxis de PromQL ni un nombre de variable del código.

Ejemplos de tipos de señal:

```text
cantidad acumulada de eventos
tasa de ocurrencia
cantidad de operaciones simultáneas
distribución de duraciones
resultado de una operación
secuencia de eventos correlacionados
```

Una respuesta como:

```text
Prometheus
```

es insuficiente. Prometheus es la herramienta que almacena y consulta métricas; no identifica la propiedad que se necesita observar.

Una respuesta como:

```text
logs
```

también es demasiado general. Debe indicarse qué información observable aportarán esos logs.

### 4.3 Columna `Evidencia necesaria`

En esta columna indique **qué datos concretos deben conservarse para poder calcular o reconstruir la señal**.

Debe pensarse en las observaciones requeridas, su unidad y las dimensiones necesarias para compararlas.

Por ejemplo, una evidencia puede requerir:

```text
una observación de duración por operación completada
resultado final de cada llamada
código o clase de estado
identificador que permita correlacionar eventos
canal de entrega
```

No incluya campos únicamente porque están disponibles en el objeto de la solicitud. Cada dato debe tener una función en la respuesta a la pregunta.

Cuando la evidencia requiera distinguir categorías, indique cuáles son esas categorías. Cuando requiera medir una duración, indique cuál intervalo se medirá.

### 4.4 Columna `Decisión de instrumentación`

En esta columna describa **cómo se producirá técnicamente la evidencia**.

La respuesta debe ser suficientemente concreta para guiar la implementación. Según el caso, puede incluir:

- tipo de instrumento: `Counter`, `Gauge`, `Histogram` o evento de log;
- familia de métricas utilizada;
- lugar del flujo donde se registra la observación;
- labels necesarios;
- buckets del histograma;
- campos que formarán parte de un evento estructurado;
- identificador utilizado para correlación.

No escriba únicamente:

```text
usar un Counter
```

si la respuesta requiere además decidir qué incrementa el contador y qué dimensiones debe conservar.

Tampoco escriba una consulta PromQL en esta columna. La consulta se construirá después de producir correctamente la evidencia.

### 4.5 Ejemplo de llenado

El siguiente ejemplo utiliza una pregunta que **no pertenece al laboratorio**.

| Pregunta operacional | Señal | Evidencia necesaria | Decisión de instrumentación |
|---|---|---|---|
| ¿Cuánto tiempo esperan los trabajos antes de salir de una cola? | distribución de tiempos de espera | una duración en segundos por cada trabajo que abandona la cola; identificación de la cola si existen varias | observar la duración con un `Histogram` al retirar cada trabajo; utilizar un label de baja cardinalidad para la cola y buckets coherentes con los tiempos esperados |

Observe la diferencia entre las columnas:

```text
pregunta
    ↓
propiedad observable
    ↓
datos necesarios para representarla
    ↓
mecanismo concreto que producirá esos datos
```

### Paso 1. Complete primero las seis filas

No modifique la instrumentación hasta haber completado una primera versión de la tabla.

### Paso 2. Revise cada decisión

Para cada fila compruebe:

1. ¿La señal realmente permite responder la pregunta?
2. ¿La evidencia propuesta contiene la información necesaria?
3. ¿Existe algún dato propuesto que no sea necesario?
4. ¿La decisión de instrumentación indica dónde y cómo se producirá la evidencia?
5. ¿Algún label o campo produciría cardinalidad innecesaria o expondría información sensible?

Después de esta revisión puede comenzar la implementación.

---

# Parte II. Logging estructurado

## 5. Rediseñar los logs de `notifications-api`

Trabaje primero en:

```text
services/notifications-api/src/notifications.ts
```

### Paso 1. Identifique los eventos del flujo

Recorra el método `createNotification` y marque los puntos en los que ocurre un hecho operacional relevante.

Como mínimo, considere:

- recepción de una solicitud válida;
- inicio de una interacción con la dependencia;
- respuesta satisfactoria;
- condición recuperable que conduce a reintento;
- fallo final;
- error inesperado.

No es obligatorio producir un log en cada línea ni en cada `if`. Cada evento debe representar un hecho que pueda ser útil posteriormente para reconstruir la operación.

### Paso 2. Asigne nombres estables a los eventos

Cada evento operacional debe incluir un campo:

```text
event
```

El valor debe ser estable y apropiado para consulta con `jq`.

Evite utilizar como identificador del evento una oración destinada a lectura humana.

### Paso 3. Seleccione el contexto

Para cada evento decida qué campos necesita conservar.

Considere, cuando sean pertinentes:

```text
request_id
notification_id
channel
template
attempt
dependency
resultado
```

`request_id` ya es incorporado por la infraestructura de `pino-http` cuando se utiliza el logger asociado con la solicitud.

No copie automáticamente todo `req.body` dentro del log.

### Paso 4. Revise la información sensible

Los siguientes valores no deben persistirse en los logs del laboratorio:

```text
recipient
message
```

El hecho de que un campo esté presente en la solicitud no constituye una razón para registrarlo.

### Paso 5. Asigne niveles

Revise cada evento y determine si corresponde a:

```text
INFO
WARN
ERROR
```

Utilice el significado operacional del hecho.

En particular, distinga entre:

- una condición anómala de la que la operación todavía puede recuperarse;
- un fallo que determina el resultado final de la operación.

### Paso 6. Compile

Ejecute:

```bash
npm run build
```

Corrija cualquier error antes de continuar.

---

## 6. Rediseñar los logs de `delivery-provider`

Trabaje en:

```text
services/delivery-provider/src/delivery.ts
```

### Paso 1. Identifique los resultados posibles

El proveedor puede producir:

```text
success
timeout
retryable_error
```

Revise qué eventos son necesarios para reconocer esos resultados posteriormente.

### Paso 2. Defina eventos estructurados

Cada evento operacional debe contener:

```text
event
```

y el contexto mínimo necesario para interpretar la operación.

### Paso 3. Mantenga la correlación

Los eventos generados por `delivery-provider` deben conservar el mismo `request_id` recibido desde `notifications-api`.

No genere un identificador nuevo para representar la misma solicitud distribuida.

### Paso 4. Revise los niveles

El primer fallo del escenario `retry-success` es recuperable desde la perspectiva del flujo completo.

La instrumentación debe permitir distinguir esa condición de un fallo final.

### Paso 5. Revise la privacidad

Compruebe nuevamente que:

```text
recipient
message
```

no se persistan.

### Paso 6. Compile

Ejecute:

```bash
npm run build
```

---

## 7. Comprobar privacidad

### Paso 1. Limpie los logs

```bash
npm run reset
```

### Paso 2. Ejecute la prueba de privacidad

```bash
npm run probe:privacy
```

La prueba envía deliberadamente:

```text
recipient = DO-NOT-LOG@example.test
message   = SECRET-LAB3-DO-NOT-PERSIST-9381
```

### Paso 3. Inspeccione los eventos

Examine ambos archivos:

```text
logs/notifications-api.jsonl
logs/delivery-provider.jsonl
```

Puede utilizar `jq` para mostrar los registros producidos:

```bash
jq '.' logs/notifications-api.jsonl
```

```bash
jq '.' logs/delivery-provider.jsonl
```

### Paso 4. Compruebe que los valores no aparezcan

Puede realizar una verificación textual adicional:

```bash
grep -F 'DO-NOT-LOG@example.test' logs/*.jsonl
```

```bash
grep -F 'SECRET-LAB3-DO-NOT-PERSIST-9381' logs/*.jsonl
```

Si la instrumentación es correcta, esos valores no deben aparecer.

### Paso 5. Documente la decisión

En la sección **2.3 Minimización de información** de `LAB3.md`, explique qué información se excluyó y por qué no era necesaria para las preguntas operacionales del laboratorio.

---

## 8. Comprobar correlación y reintento

### Paso 1. Ejecute el probe

```bash
npm run probe:retry
```

La operación utiliza:

```text
request_id      req-retry-01
notification_id not-retry-01
```

y fuerza el comportamiento:

```text
primer intento  -> error recuperable
segundo intento -> éxito
```

### Paso 2. Consulte `notifications-api`

Utilice `jq`:

```bash
jq 'select(.request_id == "req-retry-01")' \
  logs/notifications-api.jsonl
```

### Paso 3. Consulte `delivery-provider`

```bash
jq 'select(.request_id == "req-retry-01")' \
  logs/delivery-provider.jsonl
```

### Paso 4. Ordene la secuencia

A partir de los timestamps y de los eventos observados, reconstruya:

1. qué recibió `notifications-api`;
2. cuándo invocó al proveedor;
3. qué ocurrió en el primer intento;
4. por qué se realizó un segundo intento;
5. cuál fue el resultado final.

### Paso 5. Revise la semántica de los niveles

Compruebe que los niveles permitan distinguir el fallo recuperable del fallo final.

### Paso 6. Documente la evidencia

En la sección **2.2 Reintento recuperable** de `LAB3.md` incluya:

- las consultas `jq` utilizadas;
- los eventos relevantes encontrados;
- la secuencia reconstruida;
- la justificación de los niveles.

No copie el archivo completo de logs.

---

# Parte III. Métricas Prometheus

## 9. Instrumentar `notifications-api`

Trabaje con:

```text
services/notifications-api/src/metrics.ts
services/notifications-api/src/notifications.ts
```

Las familias ya existen y sus nombres forman parte del contrato del laboratorio:

```text
notifications_http_requests_total
notifications_http_requests_in_progress
notifications_http_request_duration_seconds
```

No cambie estos nombres.

### Paso 1. Revise el `Counter`

Determine qué dimensiones necesita `notifications_http_requests_total` para responder preguntas sobre actividad y errores.

Los labels seleccionados deben representar categorías con un conjunto pequeño y estable de valores.

### Paso 2. Revise el `Gauge`

`notifications_http_requests_in_progress` debe representar cuántas solicitudes están siendo procesadas en un instante determinado.

Identifique:

1. cuándo debe incrementarse;
2. cuándo debe decrementarse;
3. qué caminos del código deben garantizar el decremento.

El valor no debe quedar incrementado después de una respuesta o una excepción.

### Paso 3. Revise el `Histogram`

`notifications_http_request_duration_seconds` debe observar una duración por solicitud.

Defina:

- qué intervalo se medirá;
- qué labels son necesarios;
- qué buckets permiten distinguir los tiempos relevantes del experimento.

Los buckets deben justificarse a partir de las escalas temporales del sistema. No utilice una lista arbitraria sin relacionarla con los comportamientos que deben observarse.

### Paso 4. Evite labels de alta cardinalidad

No utilice como labels:

```text
request_id
notification_id
recipient
message
```

Estos campos identifican valores potencialmente distintos en cada operación y no son dimensiones adecuadas para las métricas agregadas de este laboratorio.

### Paso 5. Conecte las métricas con la lógica

La existencia de una familia en `/metrics` no demuestra que esté correctamente instrumentada.

Agregue las actualizaciones necesarias en `notifications.ts` para que cada operación produzca las observaciones correspondientes.

### Paso 6. Genere una solicitud

Ejecute nuevamente:

```bash
npm run probe:privacy
```

La prueba también sirve para producir tráfico controlado.

### Paso 7. Inspeccione `/metrics`

Abra:

```text
http://localhost:3000/metrics
```

o consulte desde terminal:

```bash
curl -s http://localhost:3000/metrics
```

Localice las tres familias de aplicación.

Compruebe que existen muestras producidas por el tráfico ejecutado.

---

## 10. Instrumentar `delivery-provider`

Trabaje con:

```text
services/delivery-provider/src/metrics.ts
services/delivery-provider/src/delivery.ts
```

Las familias requeridas son:

```text
delivery_requests_total
delivery_request_duration_seconds
```

### Paso 1. Diseñe las dimensiones de `delivery_requests_total`

La métrica debe permitir distinguir los resultados relevantes de la dependencia.

Defina labels de baja cardinalidad que permitan comparar esos resultados.

### Paso 2. Diseñe el histograma

`delivery_request_duration_seconds` debe representar la duración de la operación del proveedor.

Seleccione buckets que permitan distinguir, dentro de las limitaciones de un histograma, los comportamientos:

```text
normal
slow
timeout
```

### Paso 3. Instrumente todos los caminos

Compruebe que las observaciones se produzcan para:

```text
success
timeout
retryable_error
```

Una rama que responde anticipadamente también debe quedar representada si forma parte de la métrica definida.

### Paso 4. Genere tráfico

Ejecute:

```bash
npm run probe:retry
```

### Paso 5. Inspeccione `/metrics`

Abra:

```text
http://localhost:3002/metrics
```

o:

```bash
curl -s http://localhost:3002/metrics
```

Localice las familias de aplicación y compruebe que tengan observaciones.

---

## 11. Revisar la instrumentación de métricas

Complete la sección **3. Métricas Prometheus** de `LAB3.md`.

Para cada familia indique:

- tipo de instrumento;
- labels;
- propósito operacional.

Para cada `Histogram`, documente los buckets seleccionados y explique qué partes del comportamiento permiten diferenciar.

Explique también por qué:

```text
request_id
notification_id
```

son útiles en logs pero inadecuados como labels de estas métricas.

!!! note "Métricas predeterminadas"

    En `/metrics` también aparecerán métricas de Node.js y del proceso generadas mediante `collectDefaultMetrics()`.

    Su presencia es normal. El laboratorio no requiere utilizar todas las métricas disponibles. Una señal se incorpora al análisis cuando responde una pregunta relevante.

---

# Parte IV. PromQL y dashboard

## 12. Preparar el experimento A-E

Antes de ejecutar el escenario completo, compruebe que el código actualizado esté ejecutándose.

### Paso 1. Reconstruya los contenedores

```bash
docker compose up --build -d
```

### Paso 2. Compruebe los targets

En:

```text
http://localhost:9090/targets
```

los dos targets deben permanecer `UP`.

### Paso 3. Abra Prometheus y Grafana

Prometheus:

```text
http://localhost:9090
```

Grafana:

```text
http://localhost:3001
```

No ejecute todavía el escenario completo. Primero construya y compruebe las consultas.

---

## 13. Construir las consultas PromQL

Las consultas deben desarrollarse inicialmente en Prometheus.

No comience copiando expresiones directamente en Grafana. Para cada pregunta, compruebe primero qué series devuelve la expresión y si la magnitud obtenida coincide con la pregunta.

### Consulta 1. Actividad de `notifications-api`

Construya una expresión que responda:

> ¿A qué tasa recibe solicitudes `notifications-api`?

La salida debe representar una tasa, no el valor acumulado del `Counter`.

Registre en `LAB3.md`:

- expresión;
- magnitud;
- unidad.

### Consulta 2. Proporción de respuestas 5xx

Construya una expresión que responda:

> ¿Qué proporción de solicitudes termina con respuestas 5xx?

La salida debe ser una proporción entre las respuestas 5xx y el total de solicitudes en una ventana reciente.

Compruebe qué ocurre cuando todavía no existe tráfico suficiente en la ventana.

### Consulta 3. Duración media

Construya una expresión que responda:

> ¿Cuál es la duración media de `POST /notifications`?

Utilice la información `_sum` y `_count` del histograma.

La magnitud final debe expresarse como tiempo por solicitud.

### Consulta 4. p95 de duración

Construya una expresión que responda:

> ¿Cuál es el p95 de duración de `POST /notifications`?

Utilice los buckets del histograma y la función correspondiente para estimar el cuantil.

Conserve el label `le` durante la agregación requerida por el histograma.

### Consulta 5. Resultados de `delivery-provider`

Construya una expresión que responda:

> ¿Qué tasa de resultados produce `delivery-provider`, diferenciada por resultado?

El resultado debe permitir observar por separado los resultados que haya definido en la instrumentación.

### Consulta 6. p95 de `delivery-provider`

Construya una expresión que responda:

> ¿Cuál es el p95 de duración de `delivery-provider`?

Compruebe que la unidad sea coherente con el nombre de la métrica.

### Consulta 7. Solicitudes simultáneas

Consulte:

> ¿Cuántas solicitudes se encuentran actualmente en procesamiento en `notifications-api`?

Esta consulta utiliza el `Gauge` directamente. Interprete con cuidado su naturaleza instantánea: Prometheus observa el valor únicamente en cada scrape.

### Paso final. Documente las expresiones

Incluya en `LAB3.md` las **primeras seis consultas** y, para cada una, indique:

```text
magnitud
unidad
```

No es suficiente escribir que una consulta “muestra los errores” o “mide latencia”. Debe indicarse qué valor representa realmente.

La consulta 7 se utiliza para comprobar e interpretar el `Gauge` y para construir su panel en Grafana. No es necesario agregarla a la lista de seis expresiones solicitadas actualmente en `LAB3.md`.

---

## 14. Construir el dashboard en Grafana

### Paso 1. Cree un dashboard vacío

En Grafana:

```text
Dashboards
→ New
→ New dashboard
```

Utilice el datasource Prometheus ya configurado.

### Paso 2. Cree el panel de actividad

Represente la tasa de solicitudes de `notifications-api`.

Configure:

- título que describa la magnitud;
- unidad `req/s`;
- leyenda interpretable.

### Paso 3. Cree el panel de errores

Represente la proporción de respuestas 5xx.

Configure la unidad como proporción o porcentaje de forma coherente con la consulta.

### Paso 4. Cree el panel de duración de `notifications-api`

Incluya en un mismo panel:

```text
media
p95
```

Ambas series deben utilizar la misma unidad.

### Paso 5. Cree el panel de resultados de `delivery-provider`

Represente las tasas diferenciadas por resultado.

La leyenda debe permitir identificar claramente cada categoría.

### Paso 6. Cree el panel de duración del proveedor

Represente al menos el p95 de `delivery-provider`.

Puede incorporar la media si ayuda a interpretar la distribución.

### Paso 7. Cree el panel de solicitudes concurrentes

Represente:

```text
notifications_http_requests_in_progress
```

Recuerde que se trata de un valor instantáneo observado durante el scrape.

### Paso 8. Revise el dashboard

Antes de ejecutar el experimento, compruebe:

1. ¿cada panel responde una pregunta definida?
2. ¿las unidades son correctas?
3. ¿las leyendas permiten distinguir las series?
4. ¿los títulos describen magnitudes y no únicamente nombres internos de métricas?
5. ¿el rango temporal permite observar varios minutos de ejecución?

---

## 15. Ejecutar el escenario A-E

El escenario completo dura aproximadamente seis minutos.

Ejecute:

```bash
npm run scenario
```

La carga sigue esta secuencia:

| Fase | Duración aproximada | Tasa | Comportamiento |
|---|---:|---:|---|
| A | 60 s | 1 req/s | normal |
| B | 60 s | 4 req/s | normal |
| C | 90 s | 4 req/s | 3 normales + 1 timeout por cada 4 solicitudes |
| D | 90 s | 4 req/s | proveedor lento |
| E | 60 s | 1 req/s | normal |

Mientras se ejecuta:

1. observe el dashboard;
2. identifique los cambios de fase;
3. no modifique las consultas para “forzar” una gráfica esperada;
4. registre cualquier comportamiento inesperado que deba investigarse.

Cuando finalice, seleccione en Grafana un rango temporal que contenga las cinco fases.

---

## 16. Analizar el escenario

Complete la sección **6. Análisis del escenario A-E** de `LAB3.md`.

### Paso 1. Compare A y B

Determine:

- qué cambia en la actividad;
- qué ocurre con los errores;
- qué ocurre con la duración.

Responda:

> ¿Existe evidencia suficiente para afirmar que el aumento de actividad produjo una degradación?

La respuesta debe apoyarse en las señales observadas.

### Paso 2. Analice C

Determine:

- tasa aproximada de solicitudes;
- proporción de 5xx;
- comportamiento de la media;
- comportamiento del p95;
- tasa de resultados del proveedor.

Separe observaciones de interpretación.

### Paso 3. Analice D

Compare D con C.

Determine qué señal permite distinguir:

```text
degradación de disponibilidad
```

de:

```text
degradación de duración
```

### Paso 4. Analice E

Observe la recuperación.

Explique por qué una consulta calculada con una ventana temporal puede continuar reflejando parte de la fase anterior después de que el generador de carga ya haya cambiado a E.

### Paso 5. Conserve la evidencia visual

Guarde una captura del dashboard completo en:

```text
evidence/dashboard.png
```

La captura debe incluir un rango temporal en el que puedan reconocerse las fases del experimento.

### Paso 6. Exporte el dashboard

En Grafana exporte el dashboard como JSON y guárdelo en:

```text
evidence/grafana-dashboard.json
```

El archivo debe poder importarse nuevamente en Grafana.

---

# Parte V. Alerta

## 17. Diseñar una regla de alerta

La alerta debe detectar una degradación relevante de **disponibilidad** de `notifications-api`.

No se proporcionan el umbral ni los parámetros temporales. Esos valores forman parte de la decisión que debe justificar la pareja.

### Paso 1. Seleccione la señal

Parta de una expresión PromQL relacionada con disponibilidad.

La condición debe poder diferenciar una fase con respuestas 5xx de una fase que solamente presenta mayor duración.

### Paso 2. Defina el umbral

Seleccione un valor que represente una condición suficientemente anómala para este experimento.

El umbral debe justificarse mediante el comportamiento que se desea detectar.

### Paso 3. Defina el intervalo de evaluación

Indique cada cuánto tiempo Grafana evaluará la condición.

No confunda este valor con la ventana utilizada dentro de la expresión PromQL.

### Paso 4. Defina el período pendiente

Establezca cuánto tiempo debe permanecer verdadera la condición antes de pasar a `Alerting`.

El propósito es evitar que una única evaluación aislada determine inmediatamente el estado de alerta.

### Paso 5. Revise `Keep firing for`

Defina el valor utilizado y explique su efecto sobre la transición de recuperación.

### Paso 6. Ejecute nuevamente el escenario si es necesario

La evidencia requerida debe mostrar la regla en estado:

```text
Alerting
```

durante la fase C.

No se requiere configurar un `Contact point` ni demostrar el envío de una notificación.

### Paso 7. Guarde la captura

Conserve una única captura en:

```text
evidence/alert.png
```

La imagen debe permitir reconocer:

- el nombre de la regla;
- su estado `Alerting`;
- la condición evaluada o el contexto suficiente para identificarla.

### Paso 8. Documente la configuración

Complete en `LAB3.md`:

| Elemento | Valor |
|---|---|
| Expresión PromQL | |
| Umbral | |
| Intervalo de evaluación | |
| Período pendiente | |
| `Keep firing for` | |

Explique por qué esa configuración resulta coherente con el experimento observado.

---

# Parte VI. Investigación integrada

## 18. Reconstruir una operación fallida

En esta parte se parte de un síntoma agregado y se avanza hacia una solicitud concreta.

```text
dashboard
   ↓
intervalo problemático
   ↓
PromQL
   ↓
solicitud afectada
   ↓
logs correlacionados
```

### Paso 1. Localice la fase problemática

Utilice el dashboard para identificar el intervalo correspondiente a C.

### Paso 2. Confirme la degradación con PromQL

Ejecute en Prometheus las consultas necesarias para cuantificar:

- errores;
- duración;
- resultado de `delivery-provider`.

### Paso 3. Localice una solicitud fallida

Utilice los logs de `notifications-api` para encontrar un evento que represente un fallo final durante C.

No seleccione la solicitud únicamente por conocer de antemano el patrón del generador. Utilice la telemetría producida.

### Paso 4. Obtenga su `request_id`

A partir del evento encontrado, extraiga el `request_id`.

Puede utilizar `jq` para proyectar únicamente los campos necesarios. Por ejemplo, adapte una consulta de la forma:

```bash
jq 'select(/* condición */) | {time, event, request_id}' \
  logs/notifications-api.jsonl
```

La condición concreta depende de los eventos que haya diseñado.

### Paso 5. Reconstruya la solicitud en `notifications-api`

Filtre todos los eventos con ese `request_id`:

```bash
jq 'select(.request_id == "VALOR")' \
  logs/notifications-api.jsonl
```

### Paso 6. Reconstruya la misma solicitud en `delivery-provider`

```bash
jq 'select(.request_id == "VALOR")' \
  logs/delivery-provider.jsonl
```

### Paso 7. Construya una secuencia temporal

Ordene los hechos observados.

Diferencie explícitamente:

**Evidencia**

Hechos presentes directamente en logs o métricas.

**Interpretación**

Significado atribuido a esos hechos utilizando el conocimiento del sistema.

**Hipótesis**

Explicación que todavía requeriría evidencia adicional para comprobarse.

### Paso 8. Documente la reconstrucción

Complete la sección **7. Reconstrucción de una operación afectada** de `LAB3.md`.

Incluya las consultas `jq`, pero no copie los archivos completos.

---

## 19. Establecer los límites de la evidencia

Los logs y las métricas proporcionan información diferente.

En la sección **8. Límites de la evidencia** de `LAB3.md` responda:

1. ¿Qué puede establecerse a partir de las métricas agregadas?
2. ¿Qué información adicional proporcionan los logs correlacionados?
3. Indique al menos dos preguntas relevantes que todavía no puedan responderse con precisión con la instrumentación disponible.

Para cada pregunta no resuelta, indique qué evidencia adicional sería necesaria.

No atribuya causalidad únicamente porque dos señales cambien durante un intervalo similar.

---

# Parte VII. Validación y entrega

## 20. Ejecutar la validación final

Antes de validar, asegúrese de que la versión actual del código se encuentre dentro de los contenedores:

```bash
docker compose up --build -d
```

### Paso 1. Genere la evidencia de privacidad

```bash
npm run probe:privacy
```

### Paso 2. Genere la evidencia de reintento

```bash
npm run probe:retry
```

### Paso 3. Genere tráfico de aplicación

Si después de la última reconstrucción de los contenedores todavía no ha ejecutado el escenario:

```bash
npm run scenario
```

### Paso 4. Ejecute el validador

```bash
npm run validate
```

El resultado esperado es:

```text
RESULTADO: VALIDACIÓN COMPLETA
```

El validador comprueba aspectos objetivos de la solución, entre ellos:

- compilación;
- disponibilidad de los servicios;
- existencia y observación de las familias requeridas;
- ausencia de labels prohibidos;
- targets de Prometheus;
- JSON Lines válido;
- existencia de eventos estructurados;
- presencia de `request_id`;
- correlación de `req-retry-01`;
- ausencia de los valores sensibles de la prueba.

!!! warning "Alcance del validador"

    Un resultado `VALIDACIÓN COMPLETA` no demuestra por sí mismo que la instrumentación sea de buena calidad.

    La evaluación también considera la semántica de los eventos, la selección de labels y buckets, las consultas PromQL, el dashboard, la alerta y la interpretación de la evidencia.

---

## 21. Revisar `LAB3.md`

Antes de entregar, compruebe que todas las secciones estén completas:

```text
1. Diseño de instrumentación
2. Logging estructurado
3. Métricas Prometheus
4. PromQL y dashboard de Grafana
5. Regla de alerta
6. Análisis del escenario A-E
7. Reconstrucción de una operación afectada
8. Límites de la evidencia
9. Validación
```

Las respuestas deben utilizar la evidencia obtenida durante la ejecución. Evite respuestas genéricas que podrían haberse escrito sin ejecutar el sistema.

---

## 22. Revisar los archivos de evidencia

El repositorio debe contener:

```text
evidence/
├── grafana-dashboard.json
├── dashboard.png
└── alert.png
```

No agregue al repositorio:

```text
node_modules/
logs generados
datos internos de Prometheus
datos internos de Grafana
```

Los archivos de logs se utilizan para el análisis local, pero no forman parte de la entrega.

---

## 23. Preparar el commit de entrega

### Paso 1. Revise el estado

```bash
git status
```

### Paso 2. Revise los cambios

```bash
git diff
```

Compruebe especialmente que no se haya incluido información sensible ni archivos generados innecesarios.

### Paso 3. Cree el commit final

```bash
git add .
git commit -m "Complete Lab 3"
git push
```

### Paso 4. Obtenga el SHA

```bash
git rev-parse HEAD
```

Copie el SHA completo.

### Paso 5. Entregue en Moodle

La entrega debe incluir:

```text
URL del repositorio privado
SHA exacto del commit evaluable
```

No se requiere archivo ZIP ni informe PDF adicional.

El docente evaluará el estado exacto del repositorio identificado por ese SHA.

---

# Criterios de evaluación

| Criterio | Peso |
|---|---:|
| Diseño de instrumentación | 10% |
| Logging estructurado y correlación | 20% |
| Métricas Prometheus | 20% |
| PromQL y dashboard Grafana | 20% |
| Regla de alerta | 10% |
| Investigación e interpretación integrada | 20% |
| **Total** | **100%** |

## Diseño de instrumentación

Se evaluará la relación entre:

```text
pregunta operacional
→ señal
→ evidencia necesaria
→ decisión de instrumentación
```

Las decisiones deben ser coherentes con las preguntas y con las propiedades de cada tipo de telemetría.

## Logging estructurado y correlación

Se evaluará:

- diseño de eventos estables;
- selección de contexto;
- uso de `request_id`;
- distinción entre `request_id` y `notification_id`;
- niveles;
- minimización de información;
- capacidad de reconstruir el reintento y una solicitud fallida.

## Métricas Prometheus

Se evaluará:

- selección del tipo de instrumento;
- ubicación de las observaciones;
- labels;
- cardinalidad;
- buckets;
- cobertura de los distintos caminos de ejecución.

## PromQL y dashboard Grafana

Se evaluará:

- corrección de las magnitudes calculadas;
- unidades;
- interpretación de Counters e Histograms;
- capacidad del dashboard para comparar actividad, errores, duración y comportamiento de la dependencia;
- claridad de títulos y leyendas.

## Regla de alerta

Se evaluará:

- señal seleccionada;
- condición;
- umbral;
- intervalo de evaluación;
- período pendiente;
- justificación;
- evidencia del estado `Alerting`.

## Investigación e interpretación integrada

Se evaluará la capacidad de pasar de una señal agregada a una operación concreta y distinguir:

```text
evidencia
interpretación
hipótesis
límite de la evidencia
```

Una conclusión debe indicar qué observaciones la sustentan y qué aspectos no pueden establecerse con la telemetría disponible.
