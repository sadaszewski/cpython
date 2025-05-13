This repository holds a pipeline operator implementation for Python.

See: [here](https://sadaszewski.github.io/python-pipeline-operator/dist/console.html) for Pyodide-based demonstration directly in the browser.

The syntax is the following:

```python
[1, 2, 3] |> map(str) |> ", ".join() |> _.split()
```

The _ plays again its role of a soft keyword here and acts as a placeholder to indicate where to insert the results of the LHS into the RHS. If no placeholder is placed, the LHS is injected as the LAST argument. This plays better with existing Python functions. The RHS must be a call.

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
