# Gitee 上传流程说明

这份说明只针对当前仓库里 `application` 目录的日常提交场景，目的是让你在修改应用代码后，可以稳定地提交并推送到 Gitee。

## 1. 先确认当前状态

```bash
# 进入仓库根目录
cd /home/cool/fbb_bs2x/src

# 查看当前分支和工作区状态
git status --short --branch

# 查看远端地址，确认是不是你的 Gitee 仓库
git remote -v
```

## 2. 只提交 application 的标准流程

```bash
# 先把 application 目录里的改动加入暂存区
git add application

# 再确认暂存区里只有 application 相关内容
git status --short

# 提交到本地仓库
git commit -m "Add application code"

# 推送到远端 Gitee 仓库
git push origin master
```

## 3. 每一步在做什么

- `git add application`：只把 `application` 目录的修改放进提交，其他目录不会被一起提交。
- `git status --short`：检查有没有误把别的文件也加进去。
- `git commit -m "..."`：把这次 `application` 的修改保存成一个本地提交。
- `git push origin master`：把本地提交上传到 Gitee。

## 4. 中文注释版示例

```bash
# 切换到项目根目录，避免在错误目录里执行 Git 命令
cd /home/cool/fbb_bs2x/src

# 只添加 application，避免把 build、output、临时文件一起提交
git add application

# 检查暂存结果，确认只有 application
git status --short

# 创建提交，提交说明尽量写清楚这次改了什么
git commit -m "Update application"

# 推送到 Gitee
git push origin master
```

## 5. 注意点

- 如果远端有人先提交了代码，建议先执行 `git pull origin master` 再提交，避免推送被拒绝。
- 如果你本地只想撤销 `application` 的暂存，可以用 `git restore --staged application`。
- 如果你只想撤销 `application` 的本地修改，可以用 `git restore application`。
- 不要把 `build/`、`output/`、`__pycache__/` 这类临时产物一起提交，除非你非常确定需要。
- 如果 `git push` 提示权限或认证失败，先检查远端地址和 SSH Key 是否已在 Gitee 配好。

## 6. 推荐的日常顺序

```bash
# 先同步远端，减少冲突
git pull origin master

# 修改 application 下的文件

# 只提交 application
git add application
git commit -m "Describe your change"

# 推送到 Gitee
git push origin master
```

## 7. 如果你要回退

- 还没 `git add`：直接改文件或用编辑器撤销。
- 已经 `git add` 但还没 `git commit`：用 `git restore --staged application`。
- 已经 `git commit` 但还没 `git push`：可以用 `git reset --soft HEAD~1`，然后重新整理提交。
- 已经 `git push` 到远端：优先新建一个修正提交，不建议直接硬改历史。

## 8. 一句话总结

以后你只改 `application`，就按“先同步 -> 修改 -> `git add application` -> `git commit` -> `git push`”这个顺序走就行。