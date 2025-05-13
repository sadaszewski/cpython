This repository holds a pipeline operator implementation for Python.

See: [here](https://sadaszewski.github.io/python-pipeline-operator/dist/console.html) for Pyodide-based demonstration directly in the browser.

The syntax is the following:

```python
[1, 2, 3] |> map(str) |> ", ".join() |> _.split()
```
