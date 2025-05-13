This repository holds a pipeline operator implementation for Python.

See: [here](https://sadaszewski.github.io/python-pipeline-operator/dist/console.html) for Pyodide-based demonstration directly in the browser.

The syntax is the following:

```python
[1, 2, 3] |> map(str) |> ", ".join() |> _.split()
```

Can pass the placeholder by position:

```python
5 |> range(1, _) |> list()
```

```python
1 |> range(_, 5) |> list()
```

Or keyword:

```python
5 |> sum([1, 2, 3, 4, 5], start=_)
```

Only one placeholder is allowed, since everything happens on the stack!
